
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
#include "player.h"

static t_playstatus curstat = pl_stop;

static int curpid = 0;
static t_track *curtrack = NULL;

static time_t nextstart = 0;
static t_track *nexttrack = NULL;


/* used by player_start: */
t_player_func_update player_func_resume = NULL;
/* used by starttrack: */
t_player_func_update player_func_newtrack = NULL;
/* used by update_status: */
t_player_func_update player_func_pause = NULL;
t_player_func_update player_func_stop = NULL;




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
				track_setlastplay(curtrack, time(NULL) );
				track_save(curtrack);
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
	t_track *t;

	if( NULL == (t = queue_fetch() ))
		if( NULL == ( t = random_fetch() ))
			return NULL;

	// TODO: use stat() to verify existance
	return t;
}

static t_playerror startplay( void )
{
	t_playstatus wantstat = pl_play;
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

	/* get next track when there is no pre-fetched one */
	if( nexttrack == NULL )
		nexttrack = getnext();

	/* still no track? queue must be empty */
	if( NULL == nexttrack )
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
				nexttrack->fname );
		execlp( opt_player, opt_player, nexttrack->fname, NULL );

		syslog( LOG_ERR, "exec of player failed: %m");
		exit( -1 );

	}
	
	/* parent */
	curstat = pl_play;
	curpid = pid;
	curtrack = nexttrack;

	if( player_func_newtrack )
		(*player_func_newtrack)();

	/* pre-fetch next track */
	nexttrack = getnext();

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

	if( curstat == pl_stop )
		return PE_NOTHING;

	/* loop didn't run a single time - it wasn't playint */
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
	t_playstatus wantstat = pl_play;

	if( PE_OK != terminate( &wantstat) )
		return PE_FAIL;

	return startplay();
}

t_playerror player_prev( void )
{
#if TODO_prev
	t_playstatus wantstat = pl_play;

	if( PE_OK != terminate(&wantstat) )
		return PE_FAIL;

	track_free(nexttrack);
	if( NULL == (nexttrack = get_prev())){
		return PE_NOTHING;
	}

	return startplay();
#else
	return PE_NOSUP;
#endif
}


t_playerror player_stop( void )
{
	t_playstatus wantstatus = pl_stop;

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

	if( curstat == pl_stop && wantstat == pl_play ){

		if( ! nextstart )
			nextstart = now + opt_gap;

		if( nextstart <= now )
			/* nextstart is reset to 0 by startplay() */
			startplay();
		else
			curstat = pl_play;

	/* player terminated although it is paused - this results in 
	 * the same status as the gap before a track */
	} else if( curstat == pl_stop && wantstat == pl_pause ){
		curstat = pl_pause;

	}
}


