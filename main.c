#define _GNU_SOURCE

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
#include <getopt.h>

#include <libncc/pidfile.h>

#include "client.h"
#include "proto.h"
#include "commondb/random.h"
#include "player.h"
#include "sleep.h"
#include "opt.h"

char *progname = NULL;
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
		earliest( &wakeup, sleep_get() );
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

		sleep_check();

		// TODO: do other things

		clients_clean();
		client_accept( &fdread );
	}
}

// TODO: config file
// TODO: sighup handler

static void usage( void );

int main( int argc, char **argv )
{
	pid_t pid;
	char *pidfile = "/var/run/dudld/dudld.pid";
	int foreground = 0;
	int debug = 0;
	int port = 4445;
	int c;
	int needhelp = 0;
	int rv;
	struct option lopts[] = {
		{ "help", no_argument, NULL, 'h' },
		{ "foreground", no_argument, NULL, 'f' },
		{ "debug", no_argument, NULL, 'd' },
		{ "port", required_argument, NULL, 'p' },
	};

	progname = argv[0];
	pid = getpid();
	while( -1 != ( c = getopt_long( argc, argv, "hfdp:",
					lopts, NULL ))){

		switch(c){
		  case 'h':
			  usage();
			  exit(0);
			  break;

		  case 'f':
			  foreground++;
			  break;

		  case 'd':
			  debug++;
			  break;

		  case 'p':
			  port = atoi(optarg);
			  break;

		  default:
			  needhelp++;
			  break;
		}
	}
	if( needhelp ){
		fprintf( stderr, "use --help for usage information\n" );
		exit( 1 );
	}

	openlog( progname, LOG_PID | LOG_PERROR, LOG_DAEMON );
	if( ! debug )
		setlogmask( LOG_UPTO(LOG_INFO) );


	if( pidfile_flock(pidfile)){
		syslog( LOG_ERR, "cannot create pidfile: %m");
		exit(1);
	}

	// TODO: use sigaction
	signal( SIGCHLD, sig_child );
	signal( SIGPIPE, SIG_IGN );

	if( clients_init( port ) ){
		perror( "clients_init()" );
		return 1;
	}

	if( !foreground && daemon(0,0) == -1 ){
		syslog( LOG_ERR, "cannot daemonize: %m" );
		exit( 1 );
	}
	/* our pid changed - update pidfile */
	if( pidfile_ftake(pidfile, pid) ){
		syslog( LOG_ERR, "cannot update pidfile: %m" );
		exit(1);
	}

	syslog(LOG_INFO, "initializing" );

	//player_init();
	player_setgap( opt_gap );
	player_setrandom( opt_random );
	proto_init();
	
	{
		expr *e = NULL;
		char *msg;
		int pos;

		if( opt_filter && *opt_filter )
			e = expr_parse_str( &pos, &msg, opt_filter );

		if( pos < 0 )
			syslog( LOG_ERR, "startup filter failed at %d: %s",
					pos, msg );

		/* at least initialize with an empty filter */
		random_setfilter( e );
	}


	syslog(LOG_INFO, "waiting" );
	rv = loop();

	syslog(LOG_INFO, "terminating" );
	clients_done();
	pidfile_funlock(pidfile);
	return rv;
}

static void usage( void )
{
	printf( "usage: %s [opt]\n", progname );
	printf(
		" -h --help          show this message\n"
		" -f --foreground    do not detach\n"
		" -d --debug         be more verbose\n"
		" -p --port <port>   set port to listen on\n"
	      );
}
