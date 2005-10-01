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

#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <syslog.h>
#include <glib.h>

#include <config.h>
#include "client.h"




t_client_func client_func_connect = NULL;
t_client_func client_func_disconnect = NULL;

static GIOChannel *lchan = NULL;
static it_client *clients = NULL;
static int maxid = 0;


/* add new element to tail */
static int it_client_add( it_client *it, t_client *c )
{
	t_client **tmp;

	if( ! it )
		return -1;

	if( NULL == (tmp = realloc(it->clients, 
					(it->num+1) * sizeof(t_client*))))
		return -1;

	it->clients = tmp;
	it->clients[it->num++] = c;
	client_addref(c);

	return 0;
}

static it_client *it_client_new( it_client *src, t_client_want_func func, void *data )
{
	it_client *it;
	int i;

	if( NULL == (it = malloc(sizeof(it_client))))
		return NULL;
	memset(it,0,sizeof(it_client));

	for( i = 0; src && i < src->num; ++i ){
		t_client *c;
		c = src->clients[i];

		if( func && ! (*func)(c,data))
			continue;
		it_client_add( it, c );
	}
	return it;
}


/* delete current element */
static void it_client_del( it_client *it )
{
	t_client *c;
	t_client **tmp;
	int remain;

	if( ! it )
		return;

	if( it->cur >= it->num )
		return;

	c = it->clients[it->cur];
	client_delref(c);

	remain = it->num - it->cur -1;
	if( remain > 0 )
		memmove( it->clients + it->cur,
			it->clients + (it->cur +1),
			remain * sizeof(t_client*));

	it->num--;

	if( it->num == 0 )
		return;


	if( NULL == (tmp = realloc(it->clients, it->num * sizeof(t_client*))))
		return;

	it->clients = tmp;
}


t_client *it_client_begin( it_client *it )
{
	if( ! it )
		return NULL;
	it->cur = 0;
	return it_client_cur(it);
}

t_client *it_client_cur( it_client *it )
{
	t_client *c;

	if( ! it )
		return NULL;
	if( it->cur >= it->num )
		return NULL;

	c = it->clients[it->cur];
	client_addref(c);
	return c;
}

t_client *it_client_next( it_client *it )
{
	if( ! it )
		return NULL;
	if( it->cur >= it->num )
		return NULL;
	it->cur++;
	return it_client_cur(it);
}

void it_client_done( it_client *it )
{
	if( ! it )
		return;

	for(it->cur = 0; it->cur < it->num; it->cur++){
		client_delref(it->clients[it->cur]);
	}
	free(it->clients);
	free(it);
}


/*
 * get new input
 */
static gboolean client_read( GIOChannel *source, 
		GIOCondition cond, gpointer data)
{
	t_client *c = (t_client*)data;
	int sock;
	int len;

	client_addref(c);

	//syslog(LOG_DEBUG,"client(%d): read", c->id );
	if( cond & G_IO_IN ){
		sock = g_io_channel_unix_get_fd(source);
		if( 0 >= (len = recv( sock, 
				c->ibuf + c->ilen,
				CLIENT_BUFLEN - c->ilen, 0))){
			goto err;
		}

		c->ilen += len;
		c->ibuf[c->ilen] = 0;

		if( c->ifunc )
			(*(t_client_func)c->ifunc)(c);

		client_delref(c);
		return TRUE;
	}

err:
	client_delref(c);
	client_close(c);
	return FALSE;
}


static gboolean client_accept( GIOChannel *source, 
		GIOCondition cond, gpointer data)
{
	int lsocket;
	t_client *c;
	int len = sizeof(c->sin);
	GIOChannel *cchan;

	(void)data;
	// TODO: proper cleanup on error

	syslog(LOG_DEBUG, "client_accept");
	if( ! (cond & G_IO_IN) ){
		syslog( LOG_ERR, "invalid condition on listen socket: %d", cond );
		return TRUE;
	}

	if( NULL == (c = malloc(sizeof(t_client))))
		return TRUE;

	lsocket = g_io_channel_unix_get_fd(source);
	if( 0 > (c->sock = accept( lsocket, (struct sockaddr*)&c->sin, &len ))){
		free(c);
		return TRUE;
	}

	/* close socket on exec */
	if( 0 > fcntl( c->sock, F_SETFD, 1 ))
		syslog( LOG_NOTICE, "setting close-on-exec flag failed: %m");

	c->id = ++maxid;
	c->user = NULL;
	c->ilen = 0;
	c->pstate = p_open;
	c->pdata = NULL;
	c->_refs = 0;
	c->ifunc = NULL;
	c->del = 0;

	if( -1 == it_client_add(clients, c ) )
		return TRUE;

	if( NULL == (cchan = g_io_channel_unix_new(c->sock))){
		client_close(c);
		syslog(LOG_ERR, "g_io_chan: %m");
		return TRUE;
	}
	g_io_add_watch(cchan, G_IO_IN | G_IO_HUP | G_IO_ERR, 
			client_read, c );


	syslog(LOG_DEBUG, "client(%d): accepted from %s", 
			c->id, inet_ntoa(c->sin.sin_addr));

	if( client_func_connect )
		(*client_func_connect)( c );

	return TRUE;
}


void client_addref( t_client *c )
{

	c->_refs++;
	syslog(LOG_DEBUG,"client(%d): addref %d", c->id, c->_refs );
}

/* free a stand-alone client structure */
void client_delref( t_client *c )
{
	--c->_refs;
	syslog(LOG_DEBUG,"client(%d): delref %d", c->id, c->_refs );

	if( c->del )
		return;

	if( c->_refs > 0 )
		return;

	syslog(LOG_DEBUG,"client(%d): free", c->id );
	c->del++;

	if( client_func_disconnect ){
		(*client_func_disconnect)( c );
	}

	g_source_remove_by_user_data(c);
	shutdown(c->sock, 2);
	user_free(c->user);
	free(c->pdata);
	free(c);
}

/* remove client from list and free the client structure */
void client_close( t_client *c )
{
	t_client *i;
	syslog(LOG_DEBUG,"client(%d): close", c->id );
	for( i = it_client_begin(clients); i; i = it_client_next(clients) ){
		if( i == c ){
			it_client_del(clients);
			client_delref(i);
			break;
		}
		client_delref(i);
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

	//syslog(LOG_DEBUG,"client(%d): send >%s<", c->id, buf );
	if( len != send( c->sock, buf, len, MSG_DONTWAIT )){
		client_close(c);
		return -1;
	}

	return 0;
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

	if( c->del )
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

	//syslog(LOG_DEBUG,"client(%d): line >%s<", c->id, line );
	return line;
}


/*
 * close all sockets
 */
void clients_done( void )
{
	int lsocket;

	syslog(LOG_DEBUG,"clients_done" );
	it_client_done(clients);

	lsocket = g_io_channel_unix_get_fd(lchan);
	g_io_channel_unref(lchan);
	g_source_remove_by_user_data(clients);
	close(lsocket);
	lchan=NULL;
}

/*
 * initialize structures
 * open listen lsocket
 */
int clients_init( int port )
{
	int lsocket;
	struct protoent *prot;
	struct sockaddr_in sin;
	int reuse;

	if( NULL == (clients = it_client_new(NULL, NULL, NULL)))
		return -1;

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

	if( NULL == (lchan = g_io_channel_unix_new(lsocket))){
		clients_done();
		return -1;
	}

	g_io_add_watch(lchan, G_IO_IN | G_IO_HUP | G_IO_ERR, 
		client_accept, clients );

	return 0;
}


t_client *client_get( int id )
{
	t_client *c;
	for( c = it_client_begin(clients); c; c = it_client_next(clients) ){
		if( c->id == id ){
			return c;
		}
		client_delref(c);
	}
	return NULL;
}

static int check_minperm( t_client *c, void *data )
{
	return ( c->user ? c->user->right : r_any ) >= *(t_rights*)data;
}

static int check_uid( t_client *c, void *data )
{
	int *uid = (int*)data;

	if( c && c->user && c->user->id == *uid )
		return 1;

	return 0;
}

int client_bcast( const char *buf, t_client_want_func func, void *data )
{
	t_client *c;

	for( c = it_client_begin(clients); c; c = it_client_next(clients) ){
		if( (*func)(c, data) )
			client_send(c, buf);

		client_delref(c);
	}
	return 0;
}

int client_bcast_perm( const char *buf, t_rights minperm )
{
	return client_bcast( buf, check_minperm, &minperm );
}

it_client *clients_list( void )
{
	return it_client_new( clients, NULL, NULL );
}


it_client *clients_uid( int uid )
{
	return it_client_new( clients, check_uid, &uid );
}


