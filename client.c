/*
 * module to manage TCP clients.
 *
 * a listening socket is opened and clients are added/removed as they go.
 *
 * a line oriented ASCII protocol is assumed:
 *
 * Received data is combined to complete lines. Each line must be retrieved
 * seperately from the client (_getline).
 *
 * on the other hand you can send arbitrary strings to the client.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <syslog.h>

#include "client.h"



t_client *clients = NULL;

t_client_func client_func_connect = NULL;
t_client_func client_func_disconnect = NULL;

static int maxid = 0;
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

	/* re-use a previos socket */
	reuse = 1; 
	if( 0 > setsockopt( lsocket, SOL_SOCKET, SO_REUSEADDR, 
			(void *) &reuse, sizeof(reuse)) ){
		return -1;
	}

	/* close socket on exec() */
	if( 0> fcntl( lsocket, F_SETFD, 1 ))
		return -1;

	/* and bind to wanted port */
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
 * if there are pending connections, accept one
 * yes, you must use select and pass the read fdset to this function.
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

	/* close socket on exec */
	if( 0 > fcntl( c->sock, F_SETFD, 1 ))
		syslog( LOG_NOTICE, "setting close-on-exec flag failed: %m");

	c->id = ++maxid;
	c->close = 0;
	c->user = NULL;
	c->ilen = 0;
	c->pstate = p_open;
	c->pdata = NULL;

	c->next = clients;
	clients = c;

	if( client_func_connect )
		(*client_func_connect)( c );

	return c;
}


/*
 * mark a client to be closed
 * this is necessary, to be able to close clients when walking the linear
 * list.
 */
void client_close( t_client *c )
{
	c->close++;
}

/*
 * do the real work for disconnect
 */
static void client_free( t_client *c )
{
	shutdown(c->sock, 2);
	user_free(c->user);
	free(c->pdata);
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
		if(  c->close ){
			t_client *s = c;

			c = c->next;
			if( l ){
				l->next = c;
			} else {
				clients = c;
			}

			s->next = NULL;
			if( client_func_disconnect )
				(*client_func_disconnect)( s );

			client_free( s );

		} else {
			l = c;
			c = c->next;
		}

	}
}

/*
 * write a message to a client
 * shouldn't block - untested.
 */
int client_send( t_client *c, const char *buf )
{
	int len = strlen(buf);

	if( ! len )
		return 0;

	if( c->close )
		return -1;

	if( len != send( c->sock, buf, len, MSG_DONTWAIT )){
		client_close(c);
		return -1;
	}

	return 0;
}

/*
 * get new input
 * yes, you must use select and pass the read fdset to this function.
 */
void client_poll( t_client *c, fd_set *read )
{
	int len;

	if( c->close )
		return;

	if( ! FD_ISSET(c->sock,read))
		return;

	if( 0 >= (len = recv( c->sock, 
			c->ibuf + c->ilen,
			CLIENT_BUFLEN - c->ilen, 0))){
		client_close(c);
		return;
	}

	c->ilen += len;
	c->ibuf[c->ilen] = 0;
}

/* 
 * find first complete line and return a newly allocated copy
 * removes this line from the clients buffer
 *
 * you must free() the line yourself
 * invoke this repeatedly, untill it returns NULL
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
			syslog( LOG_WARNING, 
					"line too long, disconnecting client");
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
 * don't forget to initialize/reset the varialbles 
 */
void clients_fdset( int *maxfd, fd_set *read )
{
	t_client *c;

	FD_SET( lsocket, read );
	largest( maxfd, lsocket );

	for( c = clients; c; c = c->next ){
		if( c->close )
			continue;

		FD_SET( c->sock, read );
		largest( maxfd, c->sock );
	}
}

/*
 * close all sockets
 */
void clients_done( void )
{
	t_client *c;

	for( c = clients; c; c = c->next )
		client_close( c );
	
	clients_clean( );

	close(lsocket);
	lsocket=0;
}


