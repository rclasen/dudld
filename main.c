
#include <stdio.h>
#include <stdlib.h>

#include "client.h"
#include "proto.h"

static void loop( void )
{
	fd_set fdread;
	int maxfd;
	int ret;
	t_client *client;

	while(1){
		FD_ZERO( &fdread );
		maxfd = 0;
		clients_fdset( &maxfd, &fdread );
		maxfd++;

		ret = select( maxfd, &fdread, NULL, NULL, NULL );
		// TODO: check select()

		for( client = clients; client; client = client->next ){
			if( client->close )
				continue;

			client_poll( client, &fdread );
			proto_input( client );
		}

		// TODO: do other things

		/* find clients that are gone */
		for( client = clients; client; client = client->next ){
			if( ! client->close )
				continue;

			proto_delclient( client );
		}

		clients_clean();

		if( NULL != (client = client_accept( &fdread ))){
			proto_newclient( client );
		}
	}
}

// TODO: sighandler
// TODO: config file
// TODO: daemoniue
// TODO: pidfile

int main( int argc, char **argv )
{
	(void) argc;
	(void) argv;
	// TODO: getopt

	if( clients_init( 4445 ) ){
		perror( "clients_init()" );
		return 1;
	}

	//player_init();
	proto_init();

	loop();

	clients_done();
	return 0;
}

