
#include <sys/types.h>
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

#include "opt.h"
#include "track.h"
#include "queue.h"
#include "proto.h"
#include "player.h"

typedef enum {
	c_none,
	c_syserr,
	c_ign,
	c_pause,
	c_term,
	c_died,
} t_childstat;

t_player_func_update player_func_newtrack = NULL;
t_player_func_update player_func_pause = NULL;
t_player_func_update player_func_resume = NULL;
t_player_func_update player_func_stop = NULL;

/* time to start next track at. Only valid in pl_gap */
static time_t startat = 0;

/* status of internal state machine. always use a switch() statement
 * without a default: tag to check this variable */
static t_playstate state = pl_stop;

/* currently playing track. only valid in pl_play or pl_pause state */
static t_track *current = NULL;

/* PID of currently running player. only valid in pl_play or pl_pause
 * state */
static int playpid = 0;


/************************************************************
 * internal functions
 */

/*
 * TODO: check kill() usage
 * hmm, kill fails, when the child already terminated - legitimate or not
 * actions like _stop() currently assume that kill succeeds
 */

/*
 * get next track to play
 */
static t_track *p_getnext( void )
{
	t_track *t;

	if( NULL == (t = queue_fetch() ))
		if( NULL == ( t = random_fetch() ))
			return NULL;

	// TODO: use stat() to verify existance
	return t;
}

/*
 * start playing specified track
 */
static t_playerror p_start( t_track *track )
{
	int pid;

	switch(state){
		case pl_stop:
		case pl_gap:
			break;
		case pl_play:
		case pl_pause:
		case pl_gpause:
			return PE_BUSY;
	}

	startat = 0;
	
	pid = fork();

	/* fork failed */
	if( pid < 0 ){
		syslog( LOG_ERR, "cannot fork player: %m");
		return PE_SYS;
	}

	/* child */
	if( pid == 0){
		int fd;
		char *fname;

		if( NULL == (fname = malloc( strlen(track->dir) + 
						strlen(track->fname) + 10 ))){
			syslog( LOG_ERR, "cannot allocate filename: %m" );
			exit( -1);
		}
		sprintf( fname, "%s/%s", track->dir, track->fname );

		// TODO: redirect output to a file
		if( 0 > (fd = open( "/dev/null", O_RDWR, 0700 ))){
			syslog( LOG_ERR, "cannot open player output: %m" );
			exit( -1 );
		}

		dup2(fd, STDIN_FILENO );
		dup2(fd, STDOUT_FILENO );
		dup2(fd, STDERR_FILENO );
		if( fd != STDIN_FILENO && 
				fd != STDOUT_FILENO && 
				fd != STDERR_FILENO )
			close( fd );

		/* create a session to kill all children spawned by the
		 * player from _stop() */
		setsid();

		syslog( LOG_DEBUG, "starting %s %s", opt_player, fname );
		execlp( opt_player, opt_player, fname, NULL );

		syslog( LOG_ERR, "exec of player failed: %m");
		exit( -1 );

	}
	
	/* parent */
	state = pl_play;
	playpid = pid;
	current = track;

	if( player_func_newtrack )
		(*player_func_newtrack)();

	return PE_OK;
}

/*
 * check for terminated child
 * used by pause, stop and check
 *
 * return:
 *  c_none	no child terminated
 *  c_syserr	system error
 *  c_ign	child / request ignored
 *  c_pause	child paused
 *  c_term	child terminated propperly
 *  c_died	child died unexpectedly
 */
static t_childstat p_checkchild( void )
{
	int pid;
	int rv;
	int unexpected = 0;

	if( 0 > (pid = waitpid( playpid, &rv, WNOHANG | WUNTRACED ))){
		if( errno == ECHILD )
			return c_none;

		return c_syserr;
	}

	/* nothing terminated */
	if( pid == 0 )
		return c_none;

	/* although we asked only for the player, we got another 
	 * child and ignore it */
	if( playpid != pid ){
		return c_ign;
	}

	/* paused */
	if( WIFSTOPPED(rv)){
		// TODO: use switch for state
		if( state >= pl_play ){
			state = pl_pause;
			if( player_func_pause )
				(*player_func_pause)();
			return c_pause;
		}

		syslog( LOG_NOTICE, 
				"huh? child paused, when none is running?" );
		return c_ign;
	}

	/* normal exit */
	if( WIFEXITED(rv) ){
		if( WEXITSTATUS(rv)){
			syslog( LOG_ERR, "player returned %d, stopping",
					WEXITSTATUS(rv));
			unexpected ++;
		}

	/* terminated by signal */
	} else if( WIFSIGNALED(rv) ){
		/* we send SIGTERM and SIGKILL */
		switch( WTERMSIG(rv) ){
		  case SIGTRAP:
			return c_ign;

		  case SIGTERM:
		  case SIGKILL:
			break;

		  default:
			syslog( LOG_ERR, "player got signal %d, stopping",
					WTERMSIG(rv));
			unexpected ++;
		}

	/* why did we get here ?? */
	} else {
		return c_died;
	}

	if( unexpected ){
		if( player_func_stop )
			(*player_func_stop)();

	} else {
		/* the player might have been killed to skip to another
		 * track - do not send a _stop event */

		// TODO: finish track

	}

	current = NULL;
	playpid = 0;
	state = pl_stop;

	return unexpected ? c_died : c_term;
}

/*
 * stop playing
 */
static t_playerror p_stop( void )
{
	t_childstat l, r;
	int i;

	switch(state){
		case pl_stop:
			return PE_NOTHING;
		case pl_gpause:
		case pl_gap:
			startat = 0;
			state = pl_stop;
			return PE_OK;
		case pl_play:
		case pl_pause:
			break;
	}

	assert(playpid != 0);

	if( 0 > kill(-playpid, SIGTERM ))
		return PE_SYS;

	/* wait for child to terminate */
	l = c_none;
	for( i = 5; i > 0; --i ){

		while( c_none != (r = p_checkchild() )){
			switch( r ){
				case c_syserr:
					return PE_SYS;
				case c_ign:
					continue;
				case c_none: /* already dealt with */
				case c_term:
				case c_died:
				case c_pause:
			}

			l = r;
		}

		switch( l ){
			case c_syserr:
				return PE_SYS;
			case c_term:
			case c_died:
				return PE_OK;
			case c_pause:
				return PE_FAIL;
			case c_ign:
			case c_none:
		}

		if( i > 1 ){
			sleep(1);

			/* the child might be stopped - try to wake it up */
			kill(-playpid, SIGCONT );
			kill(-playpid, SIGTERM );
		}
	}

	/* kill -TERM failed - try harder */
	if( 0 > kill(-playpid, SIGKILL))
		return PE_SYS;

	r = p_checkchild();
	switch( r ){
		case c_syserr: 
			return PE_SYS;
		case c_term:
		case c_died:
			return PE_OK;
		case c_pause:
		case c_none:
		case c_ign:
			return PE_FAIL;
	}

	return PE_FAIL;
}

/*
 *  pause playing
 */
static t_playerror p_pause( void )
{
	int i;
	t_childstat l, r;

	switch( state ){
		case pl_stop:
			return PE_NOTHING;
		case pl_gap:
			state = pl_gpause;
			return PE_OK;
		case pl_pause:
		case pl_gpause:
			return PE_BUSY;
		case pl_play:
			break;
	}

	assert(playpid != 0 );

	if( 0 > kill( -playpid, SIGSTOP ))
		return PE_SYS;

	l = c_none;
	for( i = 5; i > 0; --i ){
		while( 0 != ( r = p_checkchild() )){
			switch( l ){
				case c_syserr: 
					return PE_SYS;
				case c_ign:
					continue;
				case c_term:
				case c_died:
				case c_none:
				case c_pause:
			}

			l = r;
		}

		switch( l ){
			case c_syserr: 
				return PE_SYS;
			case c_pause:
				return PE_OK;
			case c_term:
			case c_died:
				return PE_FAIL;
			case c_none:
			case c_ign:
		}

		if( i > 1 )
			sleep(1);
	}

	return PE_FAIL;
}

/*
 * resume a paused player
 */
static t_playerror p_resume( void )
{
	switch(state){
		case pl_stop:
		case pl_gap:
		case pl_play:
			return PE_NOTHING;
		case pl_gpause:
			p_stop();
			return player_start();
		case pl_pause:
			break;
	}

	assert( playpid != 0 );

	if( 0 > kill( -playpid, SIGCONT ))
		return PE_SYS;

	state = pl_play;
	if( player_func_resume )
		(*player_func_resume)();

	return PE_OK;
}


/************************************************************
 * interface functions
 */


/*
 * return currently playing track
 */
t_track *player_track( void )
{
	return current;
}

/*
 * return current play status
 */
t_playstate player_status( void )
{
	return state;
}


/*
 * resume playback or 
 * start playing next avail track and tell this to clients
 */
t_playerror player_start( void )
{
	int r;
	t_track *track;

	switch( state ){
		case pl_pause:
		case pl_gpause:
			return p_resume();
		case pl_play:
		case pl_gap:
			return PE_BUSY;
		case pl_stop:
			break;
	}

	if( NULL == (track = p_getnext()))
		return PE_NOTHING;

	if( PE_OK != (r = p_start( track )))
		return r;

	return PE_OK;
}

/*
 * stop playing
 */
t_playerror player_stop( void )
{
	int r;

	if( PE_OK != (r = p_stop()))
		return r;

	if( player_func_stop )
		(*player_func_stop)();

	return PE_OK;
}

/*
 * abort currently playing track (if any)
 * and play next
 */
t_playerror player_next( void )
{
	int r;

	r = p_stop();
	if( !( r == PE_OK || r == PE_NOTHING))
		return r;

	return player_start();
}

/*
 * abort currently playing track (if any)
 * and play previous
 */
t_playerror player_prev( void )
{
	return PE_NOSUP;

#ifdef todo_prev
	r = p_stop();
	if( !( r == PE_OK || r == PE_NOTHING))
		return r;

	if( NULL == (track = get_previous()))
		return PE_NOTHING;

	if( PE_OK != (r = p_start( track )))
		return r;

	return PE_OK;
#endif
}

/*
 * pause playing
 */
t_playerror player_pause( void )
{
	return p_pause();
}

/*
 * resume paused playing
 */
t_playerror player_resume( void )
{
	return p_resume();
}

/* 
 * - invoke periodicaly and optionally after a sigchild
 */
t_playerror player_check( void )
{
	t_childstat r, l;
	time_t now;

	l = c_none;
	while( c_none != ( r = p_checkchild())){
		switch( r ){
			case c_syserr:
				return PE_SYS;
			case c_ign:
				continue;
			case c_none: /* already dealt with */
			case c_term:
			case c_died:
			case c_pause:
		}

		l = r;
	}

	now = time(NULL);

	switch( l ){
		case c_syserr:
			return PE_SYS;
		case c_term:
			/* schedule next track */
			state = pl_gap;

			/* startat is reset by p_start and p_stop */
			startat = now;
			if( opt_gap ){
				startat += opt_gap;
			}
			break;

		case c_died:
			return PE_FAIL;

		case c_none: 
		case c_ign:
		case c_pause:
	}

	/* play next track */
	if( state == pl_gap && startat <= now ){
		state = pl_stop;
		return player_start();
	}

	return PE_OK;
}

/*
 * return the time of the next wakeup
 */
time_t player_wakeuptime( void )
{
	if( state != pl_gap )
		return 0;

	return startat;
}



