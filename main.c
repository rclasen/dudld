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
#include <glib.h>
#include <popt.h>

#include <libncc/pidfile.h>

#include "client.h"
#include "proto.h"
#include "commondb/random.h"
#include "commondb/sfilter.h"
#include "player.h"
#include "sleep.h"
#include "opt.h"

char *progname = NULL;
GMainLoop *gmain = NULL;

static void sig_term( int sig )
{
	(void)sig;
	g_main_loop_quit(gmain);
}

// TODO: config file
// TODO: sighup handler - reread config

static void load_filter( void )
{
	int id;
	t_sfilter *sf;
	expr *e = NULL;
	char *msg;
	int pos;

	if( ! opt_sfilter || ! *opt_sfilter )
		goto done;

	if( -1 == (id = sfilter_id( opt_sfilter )))
		goto done;

	if( NULL == (sf = sfilter_get( id )))
		goto done;

	e = expr_parse_str( &pos, &msg, sf->filter );
	if( e == NULL ){
		syslog( LOG_ERR, "startup filter failed at %d: %s",
				pos, msg );
	}
	sfilter_free(sf);

done:
	/* at least initialize with an empty filter */
	random_setfilter( e );
	expr_free(e);
}

static void save_filter( void )
{
	char buf[4096];
	expr *e;
	int id;

	if( ! opt_sfilter || ! *opt_sfilter )
		return;

	if( -1 == (id = sfilter_id( opt_sfilter )))
		return;

	if( NULL == (e = random_filter()))
		return;

	expr_fmt(buf, 4096, e );
	sfilter_setfilter(id, buf);
}

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
	struct poptOption popt[] = {
		{ NULL,	0, POPT_ARG_INCLUDE_TABLE, NULL, 0, "GStreamer", NULL},
		{ "help",	'h', POPT_ARG_NONE,	NULL, 'h', NULL, NULL },
		{ "foreground",	'f', POPT_ARG_NONE,	NULL, 'f', NULL, NULL  },
		{ "debug",	'd', POPT_ARG_NONE,	NULL, 'd', NULL, NULL  },
		{ "port",	'p', POPT_ARG_INT,	NULL, 'p', NULL, NULL  },
		{ "pidfile",	'i', POPT_ARG_STRING,	NULL, 'i', NULL, NULL  },
		POPT_TABLEEND
	};
	poptContext pctx;

	popt[0].arg = player_popt_table();

	progname = strrchr( argv[0], '/' );
	if( NULL != progname ){
		progname++;
	} else {
		progname = argv[0];
	}
	pid = getpid();

	pctx = poptGetContext( NULL, argc, (const char**) argv, popt, 0 );
	while( 0 < (c = poptGetNextOpt(pctx))){
		const char *optarg;

		optarg = poptGetOptArg(pctx);
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

		  case 'i':
			  pidfile = strdup(optarg);
			  break;

		  default:
			  needhelp++;
			  break;
		}
	}
	poptFreeContext(pctx);

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
	signal( SIGTERM, sig_term );
	signal( SIGINT, sig_term );
	signal( SIGCHLD, SIG_IGN );
	signal( SIGPIPE, SIG_IGN );

	if( clients_init( port ) ){
		syslog( LOG_ERR, "clients_init(): %m" );
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

	db_init();
	random_init();
	player_init();
	player_setgap( opt_gap );
	player_setrandom( opt_random );
	proto_init();
	
	load_filter();


	syslog(LOG_INFO, "waiting" );

	gmain = g_main_loop_new(NULL,0);
	g_main_loop_run(gmain);

	syslog(LOG_INFO, "terminating" );
	
	save_filter();
	player_done();
	clients_done();
	db_done();
	pidfile_funlock(pidfile);
	return 0;
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
