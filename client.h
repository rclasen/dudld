#ifndef _CLIENT_H
#define _CLIENT_H

#include <netinet/in.h>

#define CLIENT_BACKLOG 10
#define CLIENT_BUFLEN 10240

typedef struct _t_client {
	struct _t_client *next;
	int close;
	int sock;
	struct sockaddr_in sin;
	char ibuf[CLIENT_BUFLEN+1];
	int ilen;
} t_client;

extern t_client *clients;

int clients_init( int port );
void clients_done( void );
t_client *client_accept( fd_set *read );
void client_close( t_client *c );
void clients_clean( void );
int client_send( t_client *c, const char *buf );
void client_poll( t_client *c, fd_set *read );
char *client_getline( t_client *c );
void clients_fdset( fd_set *read, fd_set *write, int *maxfd );


#endif
