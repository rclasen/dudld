
#include <sys/types.h>
#include <sys/time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

#include "client.h"
#include "proto.h"
#include "player.h"
#include "opt.h"

int check_child = 0;

static void sig_child( int sig )
{
	check_child++;
	signal( sig, sig_child );
}

static void earliest( time_t *a, time_t b )
{
	if( b && b > *a )
		*a = b;
}

static int loop( void )
{
	fd_set fdread;
	int maxfd;
	time_t wakeup;
	struct timeval tv, *tvp;
	t_client *client;

	while(1){
		/* handle flag set by SIGCHLD handler */
		if( check_child ){
			player_check();
			check_child = 0;
		}

		/* initialize fdsets for select */
		FD_ZERO( &fdread );
		maxfd = 0;
		clients_fdset( &maxfd, &fdread );
		maxfd++;

		/* set timeout for select */
		tvp = NULL;
		wakeup = 0;
		earliest( &wakeup, player_wakeuptime() );
		// TODO: check other scheduled events


		if( wakeup ){
			check_child++;

			wakeup -= time(NULL);
			if( wakeup < 0 )
				wakeup = 0;
			tv.tv_sec = wakeup;
			tv.tv_usec = 0;
			tvp = &tv;
		}

		if( 0 > select( maxfd, &fdread, NULL, NULL, tvp )){
			if( errno != EINTR ){
				syslog( LOG_CRIT, "select failed: %m" );
				return 1;
			}

			/* 
			 * we got a signal - maybe sigchild. 
			 * as fdsets are invalid anyways, we restart again
			 */
			continue;
		}

		for( client = clients; client; client = client->next ){
			if( client->close )
				continue;

			client_poll( client, &fdread );
			proto_input( client );
		}

		// TODO: do other things

		clients_clean();
		client_accept( &fdread );
	}
}

// TODO: sighandler
// TODO: config file
// TODO: daemoniue
// TODO: pidfile

int main( int argc, char **argv )
{
	int rv;

	(void) argc;
	(void) argv;
	// TODO: getopt

	openlog( "xmserv", LOG_PID, LOG_DAEMON );
	// TODO: setlogmask( LOG_UPTO(LOG_INFO) );

	// TODO: use sigaction
	signal( SIGCHLD, sig_child );
	signal( SIGPIPE, SIG_IGN );

	if( clients_init( 4445 ) ){
		perror( "clients_init()" );
		return 1;
	}

	//player_init();
	player_setgap( opt_gap );
	player_setrandom( opt_random );
	proto_init();
	random_setfilter( opt_filter );

	syslog(LOG_INFO, "started" );
	rv = loop();

	syslog(LOG_INFO, "terminating" );
	clients_done();
	return rv;
}

