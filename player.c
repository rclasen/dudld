
#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/param.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <syslog.h>
#include <errno.h>
#include <assert.h>
#include <string.h>

#include "opt.h"
#include "commondb/track.h"
#include "commondb/random.h"
#include "commondb/queue.h"
#include "commondb/history.h"
#include "player.h"

#define LINELEN 4096

typedef enum {
	ple_none,
	ple_open,
	ple_read,
	ple_decode,
	ple_output,
} t_pl_err;

static int pl_rfd = -1;
static int pl_wfd = -1;
static int pl_pid = 0;
static char pl_buf[LINELEN] = "";
static t_pl_err pl_error = ple_none; /* last track's error */
static t_playstatus pl_mode = pl_stop; /* what the backend is doing */

static int do_random = 1;
static int gap = 0;

static t_playstatus mode = pl_stop; /* desired mode */
static time_t nextstart = 0;
static t_track *curtrack = NULL;
static int curuid = 0;


/* used by player_start: */
t_player_func_update player_func_resume = NULL;
/* used by starttrack: */
t_player_func_update player_func_newtrack = NULL;
/* used by update_status: */
t_player_func_update player_func_pause = NULL;
t_player_func_update player_func_stop = NULL;
/* used by player_random */
t_player_func_update player_func_random = NULL;

/************************************************************
 * database functions
 */

/*
 * get next track to play from database
 */
static t_track *db_getnext( void )
{
	t_queue *q;

	/* queue */
	while( NULL != (q = queue_fetch())){
		curtrack = queue_track(q);
		curuid = q->user->id;
		queue_free(q);

		if( track_exists(curtrack) )
			return curtrack;

		track_free(curtrack);
	}

	curuid = 0;
	if( ! do_random )
		return NULL;

	/* random */
	while( NULL != (curtrack = random_fetch())){
		if( track_exists(curtrack) )
			return curtrack;

		syslog( LOG_INFO, "skipping nonexisting track: %d", 
				curtrack->id);
		track_free(curtrack);
	}

	return NULL;
}

static void db_finish( int completed )
{
	if( ! curtrack )
		return;

	if( completed )
		history_add( curtrack, curuid );

	track_free( curtrack );
	curtrack = NULL;
	curuid = 0;
}

/************************************************************
 * backend functions
 */

static t_playerror pl_open( void )
{
	int rp[2], wp[2];

	*pl_buf = 0;
	syslog( LOG_DEBUG, "forking >%s<", opt_worker );

	if( -1 == pipe(rp) ){
		syslog( LOG_ERR, "pipe: %m" );
		goto clean1;
	}
	if( -1 == pipe(wp) ){
		syslog( LOG_ERR, "pipe: %m" );
		goto clean2;
	}

	if( -1 == (pl_pid = fork() )){
		syslog( LOG_ERR, "cannot fork worker: %m");
		goto clean3;

	} else if( pl_pid == 0 ){
		int fd;

		if( 0 > (fd = open("/dev/null", O_RDWR, 0700 ))){
			syslog( LOG_ERR, "open /dev/null: %m" );
			exit( 1 );
		}

		close(STDIN_FILENO);
		close(STDOUT_FILENO);
		close(STDERR_FILENO);

		dup2(wp[0], STDIN_FILENO);
		dup2(rp[1], STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);

		if( fd != STDERR_FILENO )
			close( fd );

		close(wp[1]);
		close(rp[0]);

		setsid();

		execl( opt_worker, opt_worker, NULL );
		syslog( LOG_ERR, "exec failed: %m" );

		exit(1);
	}

        syslog(LOG_DEBUG, "worker pid: %d", pl_pid );

	pl_rfd = rp[0];
	pl_wfd = wp[1];
	close(rp[1]);
        close(wp[0]);

	return PE_OK;


clean3:
	close(wp[0]); // ok
	close(wp[1]); // may fail

clean2:
	close(rp[0]); // may fail
	close(rp[1]); // ok
clean1:
	return PE_SYS;
}

static void pl_close( void )
{
	kill(-pl_pid,SIGTERM);
	kill(-pl_pid,SIGCONT);
	close(pl_rfd);
	close(pl_wfd);
	pl_rfd = -1;
	pl_wfd = -1;
	pl_pid = 0;
	*pl_buf = 0;
}

static void pl_bcast( char *line )
{
	if( 0 == strncmp(line, "601", 3)){
		pl_mode = pl_stop;

	} else if( 0 == strncmp(line, "602", 3)){
		pl_mode = pl_pause;
		// TODO: elapsed

	} else if( 0 == strncmp(line, "603", 3)){
		pl_mode = pl_play;
		// TODO: elapsed

	} else if( 0 == strncmp(line, "62", 2)){
		pl_error = ple_read;

	} else if( 0 == strncmp(line, "63", 2)){
		pl_error = ple_decode;

	} else if( 0 == strncmp(line, "64", 2)){
		pl_error = ple_output;

	} else {
		syslog( LOG_NOTICE, "unknown worker broadcast: %s", line );
	}
}

static char *pl_getl( void )
{
	char *ntok = pl_buf;
	int tlen = 0;
	char *line = NULL;

	for(; *ntok != '\n'; ntok++, tlen++ ){

		/* reached end of string without newline */
		if( ! *ntok ){
			int rv;

			if( tlen >= LINELEN ){
				syslog(LOG_ERR, "worker output line too long" );
				pl_close();
				return NULL;
			}

			rv = read( pl_rfd, ntok, LINELEN - tlen );
			if( rv == -1 ){
				syslog( LOG_ERR, "reading worker: %m");
				pl_close();
				return NULL;
			} else if( rv == 0 ){
				syslog( LOG_ERR, "worker died" );
				pl_close();
			}

			ntok[rv] = 0;
		}
	}

	*(ntok++) = 0;
	line = strdup(pl_buf);
	memmove(pl_buf,ntok,strlen(ntok)+1);
	syslog(LOG_DEBUG, "pl_getl: %s", line );
	return(line);
}

static char *pl_get( char *cmd )
{
	char buf[LINELEN];
	char *line = NULL;

	if( ! pl_pid && PE_OK != pl_open() )
		return NULL;

	syslog( LOG_DEBUG, "pl_get: %s", cmd );
	snprintf( buf, LINELEN, "%s\n", cmd );
	if( -1 == write( pl_wfd, buf, strlen(buf))){
		syslog( LOG_ERR, "writing worker: %m");
		pl_close();
		return NULL;
	}

	while( NULL != ( line = pl_getl() )){
		
		if( *line != '6' ) 
			return line;

		pl_bcast( line );
		free(line);
	}
	return NULL;
}

static t_playerror pl_cmd( char *cmd )
{
	char *line;
	t_playerror rv;

	if( NULL == (line = pl_get( cmd )))
		return PE_FAIL;

	if( *line == '2' ){
		rv = PE_OK;

	} else if( 0 == strncmp(line,"401",3)){
		rv = PE_NOSUP;

	} else {
		rv = PE_FAIL;
	}

	free(line);
	return rv;
}

static void pl_check( void )
{
	char *line;

	if( NULL == (line = pl_get( "status" )))
		return;

	if( 0 == strncmp(line, "201", 3 )){
		pl_mode = pl_stop;

	} else if( 0 == strncmp(line, "202", 3 )){
		pl_mode = pl_pause;
		// TODO: elapsed

	} else if( 0 == strncmp(line, "203", 3 )){
		pl_mode = pl_play;
		// TODO: elapsed

	}
	free(line);
}

static t_playerror do_pause( t_playstatus oldmode )
{
	if( mode != oldmode && player_func_pause )
		(*player_func_pause)();

	if( pl_mode != pl_pause )
		return pl_cmd("pause");

	return PE_BUSY;
}

static t_playerror do_stop( t_playstatus oldmode )
{
	if( mode != oldmode && player_func_stop )
		(*player_func_stop)();

	if( pl_mode != pl_stop )
		return pl_cmd("stop");

	return PE_BUSY;
}


static t_playerror do_play( t_playstatus oldmode )
{
	char cmd[MAXPATHLEN];
	time_t now;

	switch(pl_mode){
		case pl_play:
			return PE_BUSY;

		case pl_pause:
			if( mode != oldmode && player_func_resume )
				(*player_func_resume)();
			return pl_cmd("unpause");

		default:
			break;
	}


	/* play next file? */
        now = time(NULL);

	if( ! nextstart ){
		nextstart = now + gap;
	}

        if( nextstart && nextstart > now )
		return PE_OK;

	nextstart = 0;

	/* get next track */
	db_getnext();
	if( NULL == curtrack ){
		mode = pl_stop;
		if( mode != oldmode && player_func_stop )
			(*player_func_stop)();
		return PE_NOTHING;
	}


	strcpy(cmd, "play ");
	track_mkpath(cmd+5, MAXPATHLEN-5, curtrack);

	if( PE_OK != pl_cmd(cmd) ){
		return PE_FAIL;
	}

	if( player_func_newtrack )
		(*player_func_newtrack)();

	/* we cannot tell, if the start worked - update_status has to deal
	 * with this */
	return PE_OK;
}

static t_playerror pl_gomode( t_playstatus oldmode )
{
	/* track finished playing */
	if( oldmode != pl_stop && pl_mode == pl_stop )
		db_finish( mode != pl_stop );

	switch(mode){
		case pl_play:
			return do_play( oldmode );

		case pl_pause:
			return do_pause( oldmode );

		case pl_stop:
			return do_stop( oldmode );
	}
	return PE_FAIL;
}


/************************************************************
 * interface functions
 */


/*
 * return currently playing track
 */
t_track *player_track( void )
{
	if( ! curtrack )
		return NULL;

	track_use(curtrack);
	return curtrack;
}

/*
 * return current play status
 */
t_playstatus player_status( void )
{
	return mode;
}

int player_gap( void )
{
	return gap;
}

t_playerror player_setgap( int g )
{
	gap = g;
	return PE_OK;
}

int player_random( void )
{
	return do_random;
}

t_playerror player_setrandom( int r )
{
	int old = do_random;

	do_random = r ? 1 : 0;

	if( old != do_random && player_func_random )
		(*player_func_random)();

	return PE_OK;
}

/*
 * return the time of the next wakeup
 */
time_t player_wakeuptime( void )
{
	return nextstart;
}


/*
 ************************************************************
 * interface functions that affect mode
 */

t_playerror player_pause( void )
{
	t_playstatus oldmode = mode;
	mode = pl_pause;
	return pl_gomode(oldmode);
}

/* unpause or start playing */
t_playerror player_start( void )
{
	t_playstatus oldmode = mode;
	mode = pl_play;
	return pl_gomode(oldmode);
}

t_playerror player_next( void )
{
	t_playstatus oldmode = mode;
	mode = pl_play;
	pl_cmd("stop"); // TODO: HACK
	return pl_gomode(oldmode);
}

t_playerror player_prev( void )
{
#if TODO_prev
	t_playstatus oldmode = mode;
	mode = pl_play;
	/* TODO: queue last item */
	if( PE_OK != _player_stop() )
		return PE_FAIL;
	return pl_gomode(oldmode);
#else
	return PE_NOSUP;
#endif
}


t_playerror player_stop( void )
{
	t_playstatus oldmode = mode;
	mode = pl_stop;
	return pl_gomode(oldmode);
}

/*
 ************************************************************
 * interface house keeping fuctions
 */

void player_init( void )
{
	pl_open();
}

void player_fdset( int *maxfd, fd_set *rfds )
{
	if( pl_rfd >= 0 ){
		FD_SET(pl_rfd,rfds);
		if( pl_rfd > *maxfd )
			*maxfd = pl_rfd;
	}
}

t_playerror player_check( fd_set *rfds )
{
	t_playstatus oldmode = mode;

	/* update pl_mode */
	if( rfds && pl_rfd >= 0 && FD_ISSET(pl_rfd,rfds)){
		char *line = NULL;

		if( NULL == ( line = pl_getl() ))
			return PE_FAIL
				;
		if( *line == '6' ) {
			pl_bcast( line );
		} else {
			syslog( LOG_ERR, "unexpected worker output: %s", line );
		}
		free(line);


	} else {
		// TODO: waitpid(pl_pid) ??
		pl_check();
	}

	switch( pl_error ){
		case ple_open:
		case ple_read:
		case ple_decode:
			mode = pl_stop;
			break;

		case ple_output:
			mode =  pl_pause;
			break;

		default:
			break;
	}
	pl_error = ple_none;

	return pl_gomode(oldmode);
}


