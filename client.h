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
	struct _t_client *next;
	int id;
	t_user *user;
	int close;
	int sock;
	struct sockaddr_in sin;
	char ibuf[CLIENT_BUFLEN+1];
	int ilen;
	t_protstate pstate;
	void *pdata;
} t_client;

typedef void (*t_client_func)( t_client *client );

extern t_client *clients;
extern t_client_func client_func_connect;
extern t_client_func client_func_disconnect;

int clients_init( int port );
void clients_done( void );
t_client *client_accept( fd_set *read );
void client_close( t_client *c );
void clients_clean( void );
int client_send( t_client *c, const char *buf );
void client_poll( t_client *c, fd_set *read );
char *client_getline( t_client *c );
void clients_fdset( int *maxfd, fd_set *read );


#endif
