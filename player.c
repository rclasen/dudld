
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

static int pl_rfd = -1;
static int pl_wfd = -1;
static int pl_pid = 0;
static char pl_buf[LINELEN] = "";
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

static char *pl_getl( void )
{
	char *ntok;
	char *line;

	if( NULL == (ntok = index(pl_buf, '\n' ))){
		if( strlen(pl_buf) +1 >= LINELEN ){
			syslog(LOG_ERR, "worker output line too long" );
			pl_close();
		}
		return NULL;
	}

	*(ntok++) = 0;
	line = strdup(pl_buf);
	syslog(LOG_DEBUG, "pl_getl: %s", line );

	memmove(pl_buf,ntok,strlen(ntok)+1);
	return(line);
}

static void pl_read( void )
{
	int len;
	int rv;
	char *line;

	len = strlen(pl_buf);
	rv = read( pl_rfd, pl_buf+len, LINELEN - len );
	if( rv == -1 ){
		syslog( LOG_ERR, "reading worker: %m");
		pl_close();
		return;
	} else if( rv == 0 ){
		syslog( LOG_ERR, "worker died" );
		pl_close();
		return;
	}

	while( NULL != (line = pl_getl())){

		if( 0 == strncmp(line, "601", 3)){
			pl_mode = pl_stop;

		} else if( 0 == strncmp(line, "602", 3)){
			// TODO: elapsed
			pl_mode = pl_pause;

		} else if( 0 == strncmp(line, "603", 3)){
			// TODO: elapsed
			pl_mode = pl_play;

		} else if( 0 == strncmp(line, "61", 2)){
			if( mode != pl_stop && player_func_stop )
				(*player_func_stop)();
			mode = pl_stop;
			db_finish(0);

		} else if( 0 == strncmp(line, "62", 2)){
			if( mode != pl_stop && player_func_stop )
				(*player_func_stop)();
			mode = pl_stop;
			db_finish(0);

		} else if( 0 == strncmp(line, "63", 2)){
			if( mode != pl_stop && player_func_stop )
				(*player_func_stop)();
			mode = pl_stop;
			db_finish(0);

		} else if( 0 == strncmp(line, "64", 2)){
			if( mode != pl_pause && player_func_pause )
				(*player_func_pause)();
			mode =  pl_pause;

		} else {
			syslog( LOG_NOTICE, "unknown worker message: %s", line );
		}
		free(line);
	}
}

static t_playerror pl_send( char *cmd )
{
	char buf[LINELEN];

	if( ! pl_pid && PE_OK != pl_open() )
		return PE_SYS;

	syslog( LOG_DEBUG, "pl_send: %s", cmd );
	snprintf( buf, LINELEN, "%s\n", cmd );
	if( -1 == write( pl_wfd, buf, strlen(buf))){
		syslog( LOG_ERR, "writing worker: %m");
		pl_close();
		return PE_SYS;
	}

	return PE_OK;
}

static t_playerror pl_playnext( void )
{
	char cmd[MAXPATHLEN];
	time_t now;

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
		return PE_NOTHING;
	}


	strcpy(cmd, "play ");
	track_mkpath(cmd+5, MAXPATHLEN-5, curtrack);

	return pl_send(cmd);
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
	if( mode != pl_pause && player_func_pause )
		(*player_func_pause)();
	mode = pl_pause;

	return pl_send("pause");
}

static t_playerror player_playnext( void )
{
	t_playerror rv;

	rv = pl_playnext();
	if( rv == PE_OK ){
		mode = pl_play;
		if( player_func_newtrack )
			(*player_func_newtrack)();

	} else if( mode != pl_stop ){
		if( player_func_stop )
			(*player_func_stop)();
		mode = pl_stop;
		db_finish(0);
	}

	return rv;
}

/* unpause or start playing */
t_playerror player_start( void )
{
	if( mode == pl_play )
		return PE_NOTHING;

	if( mode == pl_pause )
		if( player_func_resume ){
			(*player_func_resume)();
		return pl_send("unpause");
	}

	return player_playnext();
}

t_playerror player_next( void )
{
	db_finish(1);
	return player_playnext();
}

t_playerror player_prev( void )
{
	/* TODO: player_prev */
	return PE_NOTHING;
}


t_playerror player_stop( void )
{
	db_finish(1);
	if( mode != pl_stop && player_func_stop )
		(*player_func_stop)();
	mode = pl_stop;
	return pl_send("stop");
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

static t_playerror player_keepplay( void )
{
	syslog(LOG_DEBUG,"player_keepplay: mode=%d pl_mode=%d", 
			 mode, pl_mode );

	if( mode != pl_play ){
		if( mode != pl_mode ){
			/* TODO: */
			syslog( LOG_NOTICE, "playmode confusion: mode=%d pl_mode=%d",
				mode, pl_mode );
			mode = pl_play;
		}
		return PE_NOTHING;
	}

	if( pl_mode == pl_play )
		return PE_NOTHING;

	if( pl_mode == pl_stop ){
		db_finish(1);
		return player_playnext();
	}
	
	if( pl_mode == pl_pause ){
		/* TODO: broadcas */
		mode = pl_pause;
	}

	return PE_NOTHING;
}

void player_checkgap( void )
{
	player_keepplay();
}

void player_checkout( fd_set *rfds )
{
	/* update pl_mode */
	if( rfds && pl_rfd >= 0 && FD_ISSET(pl_rfd,rfds)){
		pl_read();
	}

	player_keepplay();
}


