#ifndef _CLIENT_H
#define _CLIENT_H

#include <netinet/in.h>
#include <commondb/user.h>

#define CLIENT_BACKLOG 10
#define CLIENT_BUFLEN 10240


// TODO: move protocol stuff to proto module
typedef enum {
	p_any, /* as allowed level for commands */
	p_open,
	p_user,
	p_idle,
} t_protstate;

typedef struct _t_client {
	int sock;
	int id;
	t_user *user;
	struct sockaddr_in sin;
	char ibuf[CLIENT_BUFLEN+1];
	int ilen;
	t_protstate pstate;
	void *pdata;
	void *ifunc;
	int _refs;
	int del;
} t_client;

typedef struct _it_client {
	t_client **clients;
	int num;
	int cur;
} it_client;

typedef void (*t_client_func)( t_client *client );

extern t_client_func client_func_connect;
extern t_client_func client_func_disconnect;


int clients_init( int port );
void clients_done( void );

typedef int (*t_client_want_func)( t_client *client, void *data );
int client_bcast( const char *buf, t_client_want_func func, void *data );
int client_bcast_perm( const char *buf, t_rights minperm );

t_client *it_client_begin( it_client *it );
t_client *it_client_cur( it_client *it );
t_client *it_client_next( it_client *it );
void it_client_done( it_client *it );

it_client *clients_list( void );

int client_send( t_client *c, const char *buf );
char *client_getline( t_client *c );
void client_close( t_client *c );

void client_addref( t_client *c );
void client_delref( t_client *c );
t_client *client_get( int id );
it_client *clients_uid( int uid );

#endif
