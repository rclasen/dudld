
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>

#include "client.h"


t_client *clients = NULL;

static int lsocket = 0;

/*
 * initialize structures
 * open listen lsocket
 */
int clients_init( int port )
{
	struct protoent *prot;
	struct sockaddr_in sin;
	int reuse;

	if( NULL == (prot = getprotobyname( "IP" ) ))
		return -1;

	if( 0 > (lsocket = socket( AF_INET, SOCK_STREAM, prot->p_proto )))
		return -1;

	reuse = 1; 
	if( 0 > setsockopt( lsocket, SOL_SOCKET, SO_REUSEADDR, 
			(void *) &reuse, sizeof(reuse)) ){
		return -1;
	}

	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	sin.sin_addr.s_addr = INADDR_ANY;
	if( 0 > bind( lsocket, (struct sockaddr *) &sin, sizeof(sin)))
		return -1;

	if( 0 > listen( lsocket, CLIENT_BACKLOG ))
		return -1;

	return 0;
}

/*
 * close all sockets
 */
void clients_done( void )
{
	// TODO: clients_done
}

/*
 * if there are pending connections, accept one
 */
t_client *client_accept( fd_set *read )
{
	t_client *c;
	int len = sizeof(c->sin);

	if( ! FD_ISSET( lsocket, read ))
		return NULL;

	if( NULL == (c = malloc(sizeof(t_client))))
		return NULL;

	if( 0 > (c->sock = accept( lsocket, (struct sockaddr*)&c->sin, &len ))){
		free(c);
		return NULL;
	}

	c->close = 0;
	c->ilen = 0;

	c->next = clients;
	clients = c;

	return c;
}


/*
 * mark a client to be closed
 * this is necessary, to be able to close clients when walking the linear
 * list.
 */
inline void client_close( t_client *c )
{
	c->close++;
}

/*
 * do the real work for disconnect
 */
static void client_free( t_client *c )
{
	shutdown(c->sock, 2);
	free(c);
}

/*
 * disconnect clients that returned an error condition
 * this takes care of the list, too.
 */
void clients_clean( void )
{
	t_client *l, *c;
	
	c = clients;
	l = NULL;

	while( c ){
		if( c->close ){
			t_client *s = c;

			c = c->next;
			if( l )
				l->next = c;
			else
				clients = c;

			client_free( s );

		} else {
			l = c;
			c = c->next;
		}

	}
}

/*
 * write a message to a client
 */
int client_send( t_client *c, char *buf )
{
	int len = strlen(buf);

	if( c->close )
		return -1;

	if( 0 > send( c->sock, buf, len, MSG_DONTWAIT|MSG_NOSIGNAL )){
		client_close(c);
		return -1;
	}

	return 0;
}

/*
 * get new input
 */
void client_poll( t_client *c )
{
	int len;

	if( c->close )
		return;

	if( 0 > (len = recv( c->sock, 
			c->ibuf + c->ilen,
			CLIENT_BUFLEN - c->ilen, MSG_NOSIGNAL))){
		client_close(c);
		return;
	}

	if( ! len )
		return;

	c->ilen += len;
	c->ibuf[c->ilen] = 0;
}

/* 
 * find first complete line and return a newly allocated copy
 * removes this line from the clients buffer
 */
char *client_getline( t_client *c )
{
	char *s;
	char *n;
	char *line;

	if( c->close )
		return NULL;

	/* skip leading linebreaks */
	s = c->ibuf;
	while( *s && ( *s == '\n' || *s == '\r')){
		s++;
	}

	/* find next linebreak */
	if( NULL == (n = strpbrk( s, "\n\r" ))){
		// TODO: handle too long lines more gracefully
		if( c->ilen >= CLIENT_BUFLEN ){
			client_send(c, "ERROR: line too long\n");
			client_close(c);
		}

		return NULL;
	}

	/* strip linebreaks */
	do {
		*n++ = 0;
	} while( *n && ( *n == '\n' || *n == '\r'));


	/* copy line to new string and remove it from buffer */
	line = strdup( s );
	c->ilen -= n - c->ibuf;
	memmove( c->ibuf, n, c->ilen );

	return line;
}

/*
 * helper for client_fdset
 */
static inline void largest( int *a, int b )
{
	if( b > *a )
		*a = b;
}

/*
 * add sockets to FD_SETs for select()
 */
void clients_fdset( fd_set *read, fd_set *write, int *maxfd )
{
	t_client *c;

	FD_SET( lsocket, read );
	largest( maxfd, lsocket );

	for( c = clients; c; c = c->next ){
		if( c->close )
			continue;

		FD_SET( c->sock, read );
		FD_SET( c->sock, write );
		largest( maxfd, c->sock );
	}
}

