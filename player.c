
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
#include "random.h"
#include "queue.h"
#include "history.h"
#include "player.h"

static t_playstatus curstat = pl_stop;

static int do_random = 1;

static int curpid = 0;
static t_track *curtrack = NULL;

static int gap = 0;
static time_t nextstart = 0;

/* used by player_start: */
t_player_func_update player_func_resume = NULL;
/* used by starttrack: */
t_player_func_update player_func_newtrack = NULL;
/* used by update_status: */
t_player_func_update player_func_pause = NULL;
t_player_func_update player_func_stop = NULL;
/* used by player_random */
t_player_func_update player_func_random = NULL;


/* 
 * when no player pid is known, the old status is returned
 * this means curstat is not changed within the gap.
 *
 * when the player got stopped, curstat becomes pl_pause
 *
 * when the player terminated curstat is set to pl_stop
 * when the player died, wantstat is set to pl_stop, too
 *
 * when the player is gone, curstat is set to pl_stop
 *
 * you can call this function as often as you want ...
 */
static t_playstatus update_status( t_playstatus *wantstat )
{
	int failed = 0;
	int oldpid = curpid;
	int pid;
	int rv;
	t_playstatus last;


	/* nothig to check - return */
	if( curpid == 0 )
		return curstat;

	last = curstat;

	/* several (bogus??) sigchilds might have queued up */
	while( 0 < (pid = waitpid( -oldpid, &rv, WNOHANG | WUNTRACED) ) ){

		/* so - what did I tell you about bogus? *g* */
		if( pid != oldpid )
			continue;


		/* paused */
		if( WIFSTOPPED(rv)){
			curstat = pl_pause;

			/* somebody else might have sent STOP, CONT, TERM
			 * to the player - so there might still be a real
			 * sigchild be outstanding. But we are finally
			 * stopped anyways - so we can invoke the callback
			 * later
			 */

			continue;
		}

		/* no pause - we got a sigchild for a terminated player */
		
		/* normal exit */
		if( WIFEXITED(rv) ){
			if( WEXITSTATUS(rv)){
				syslog( LOG_ERR, "player returned %d, "
						"stopping", WEXITSTATUS(rv));
				failed ++;
			}

		/* terminated by signal */
		} else if( WIFSIGNALED(rv) ){
			switch( WTERMSIG(rv) ){
			  /* ignore for debugger sessions */
			  case SIGTRAP:
				continue;

			  /* we send SIGTERM and SIGKILL */
			  case SIGTERM:
			  case SIGKILL:
				break;

			  default:
				syslog( LOG_ERR, "player got signal %d, "
						"stopping", WTERMSIG(rv));
				failed ++;
			}

		}

		curpid = 0;
		curstat = pl_stop;
	} 
	
	if( pid < 0 ){
		if( errno != ECHILD ) {
			/* fatal waitpid failure - shouldn't happen */
			syslog( LOG_CRIT, "fatal waitpid error: %m" );
			exit( 1 );
		}

		/* no child found - we are definatly stopped */
		if( curpid ){
			syslog( LOG_NOTICE, "player pid not found" );
			curstat = pl_stop;
		}
	}

	/* when signals arrived our of order, a SIGSTOP might have
	 * overwritten curstop */
	if( failed ){
		curstat = pl_stop;
		*wantstat = pl_stop;
	}

	if( curstat == pl_stop ){
		curpid = 0;
		if( curtrack ){
			if( ! failed ){
				// TODO: pass queue user id to history 
				history_add(curtrack, 0 );
			}
			track_free( curtrack );
			curtrack = NULL;
		}

	}

	/* send broadcasts for what is detectable from here */
	if( last != curstat ){
		if( curstat == pl_stop && curstat == *wantstat && 
				player_func_stop )
			(*player_func_stop)();

		else if( curstat == pl_pause && player_func_pause )
			(*player_func_pause)();
	}

	return curstat;
}

static t_playerror terminate( t_playstatus *wantstat )
{
	int i;

	/* no pid - we must be in the gap - or already stopped */
	if( !curpid ){
		curstat = pl_stop;
		return PE_OK;
	}

	for( i = 0; pl_stop != update_status(wantstat) && i < 5; ++i ){

		if( i == 0 ){
			/* try to kill the process itself */
			kill( curpid, SIGTERM );
			/* and wake it up in case it was stopped */
			kill( -curpid, SIGCONT );

		} else if( i == 1 ){
			/* give it some time to terminate */
			sleep(1);

		} else if( i < 4 ){
			/* did not terminate yet, try to kill again and
			 * give it some time */
			kill( -curpid, SIGCONT );
			kill( -curpid, SIGTERM );
			sleep(1);

		} else {
			/* ok, it's not nice to me, so I'm not, too. 
			 * use the sledgehammer to terminate it */
			kill( -curpid, SIGKILL );
		}
	}

	if( curstat == pl_stop )
		return PE_OK;

	return PE_FAIL;
}


/*
 * get next track to play
 */
static t_track *getnext( void )
{
	t_queue *q;
	t_track *t;

	// TODO: use stat() to verify existance
	if( NULL != (q = queue_fetch())){
		t = queue_track(q);
		queue_free(q);
		return t;
	}

	if( ! do_random )
		return NULL;

	return random_fetch();
}

static t_playerror startplay( void )
{
	t_playstatus wantstat = pl_play;
	t_track *track;
	int pid;

	update_status(&wantstat);

	/* did update_status set wantstatus to stop? */
	if( wantstat != pl_play )
		return PE_FAIL;

	/* only start new player. Caller must set state to stop when
	 * starting from gap */
	if( curstat != pl_stop )
		return PE_BUSY;

	nextstart = 0;

	/* get next track */
	if( NULL == (track = getnext()))
		return PE_NOTHING;

	pid = fork();

	/* fork failed */
	if( pid < 0 ){
		syslog( LOG_ERR, "cannot fork player: %m");
		return PE_SYS;
	}

	/* child */
	if( pid == 0){
		int fd;

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

		syslog( LOG_DEBUG, "starting %s %s", opt_player, 
				track->fname );
		execlp( opt_player, opt_player, track->fname, NULL );

		syslog( LOG_ERR, "exec of player failed: %m");
		exit( -1 );

	}
	
	/* parent */
	curstat = pl_play;
	curpid = pid;
	curtrack = track;

	if( player_func_newtrack )
		(*player_func_newtrack)();


	/* we cannot tell, if the start worked - update_status has to deal
	 * with this */
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
	return curstat;
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
	do_random = r ? 1 : 0;
	if( player_func_random )
		(*player_func_random)();

	return PE_OK;
}

/*
 * return the time of the next wakeup
 */
time_t player_wakeuptime( void )
{
	if( curpid )
		return 0;

	return nextstart;
}


t_playerror player_pause( void )
{
	t_playstatus wantstat = pl_pause;
	int i;

	for( i = 0; pl_play == update_status(&wantstat) && i < 5; ++i ){
		kill( -curpid, SIGSTOP );
		
		if( i > 0 && i < 4 )
			sleep(1);
	}

	/* player is not running */
	if( curstat == pl_stop )
		return PE_NOTHING;

	/* loop didn't run a single time - player was already paused */
	if( i == 0 )
		return PE_BUSY;

	if( curstat != pl_pause )
		return PE_FAIL;

	return PE_OK;
}

t_playerror player_start( void )
{
	t_playstatus wantstat = pl_play;

	if( pl_pause == update_status(&wantstat) ){
		if( curpid ){
			kill( -curpid, SIGCONT );
			curstat = pl_play;

			// TODO: detect, if the player resumed externally?
			if( player_func_resume )
				(*player_func_resume)();
			return PE_OK;
		}
		curstat = pl_stop;
	}

	return startplay();
}

t_playerror player_next( void )
{
	t_playstatus last = curstat;
	t_playstatus wantstat = pl_play;
	t_playerror er;

	if( PE_OK != terminate( &wantstat) )
		return PE_FAIL;

	if( PE_OK == (er = startplay()))
		return PE_OK;

	if( last != pl_stop && player_func_stop )
		(*player_func_stop)();

	return er;
}

t_playerror player_prev( void )
{
#if TODO_prev
	t_playstatus wantstat = pl_play;

	if( PE_OK != terminate(&wantstat) )
		return PE_FAIL;

	return startplay(lasttrack);
#else
	return PE_NOSUP;
#endif
}


t_playerror player_stop( void )
{
	t_playstatus wantstatus = pl_stop;

	if( curstat == pl_stop )
		return PE_NOTHING;

	return terminate( &wantstatus );
}

void player_check( void )
{
	t_playstatus wantstat = curstat;
	time_t now;

	update_status(&wantstat);

	/* we might have detected, that the player terminated or got
	 * paused from somebody else */

	/* is it time to start the next track? */
	now = time(NULL);
	if( nextstart && nextstart < now )
		curstat = pl_stop;

	/* nothig else to do ? */
	if( curstat != pl_stop )
		return;



	/* we want to play */
	if( wantstat == pl_play ){
		if( ! nextstart )
			nextstart = now + gap;

		if( nextstart > now ){
			curstat = pl_play;
			return;
		}

		/* nextstart is reset to 0 by startplay() */
		if( PE_OK == startplay())
			return;

		/* failed to start next track - stopped */
		if( player_func_stop )
			(*player_func_stop)();

		return;
	} 
	
	/* player terminated although it is paused - this results in 
	 * the same status as the gap before a track */
	if( wantstat == pl_pause )
		curstat = pl_pause;
}


