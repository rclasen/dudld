
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

#include "opt.h"
#include "commondb/track.h"
#include "commondb/random.h"
#include "commondb/queue.h"
#include "commondb/history.h"
#include "player.h"

static t_playstatus curstat = pl_stop;

static int do_random = 1;

static int curpid = 0;
static t_track *curtrack = NULL;
static int curuid = 0;

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
	static int died_unexpected = 0;
	int oldpid = curpid;
	int pid;
	int rv;
	t_playstatus last;


	/* nothig to check - return */
	if( curpid == 0 )
		return curstat;

	last = curstat;

	/*
	 * the external player was run in a process group. Try to reap all
	 * children from this group.
	 *
	 * We pick the exit status from only from the main process.
	 *
	 * so we might end up with:
	 *  - main process is dead, but its children are still playing
	 *  - a child of the main process died and we have to reap it
	 *    unexpectedly
	 */
	while( 0 < (pid = waitpid( -oldpid, &rv, WNOHANG | WUNTRACED) ) ){

		/* ignore exit status of non-main children */
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
				syslog( LOG_ERR, "player %d returned %d",
						oldpid, WEXITSTATUS(rv));
				//TODO: kill group, master is dead
				died_unexpected ++;
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
				syslog( LOG_ERR, "player %d got signal %d",
						oldpid, WTERMSIG(rv));
				died_unexpected ++;
				//TODO: kill group, master is dead
			}

		}

	} 
	
	/* pid > 0 is handled above */

	/* there are outstanding children */
	if( pid == 0 ) {
		if( last != curstat && 
				curstat == pl_pause && 
				player_func_pause )
			(*player_func_pause)();

		return curstat;
	} 

	/* pid is < 0 -> waitpid had problems finding our children */

	if( errno != ECHILD ) {
		/* fatal waitpid failure - shouldn't happen */
		syslog( LOG_CRIT, "fatal waitpid error: %m" );
		exit( 1 );
	}

	/* no children left - player definetly stopped */
	curstat = pl_stop;
	curpid = 0;

	if( died_unexpected ){
		syslog( LOG_ERR, "player %d died unexpected - "
				"stopping playback", oldpid );

		/* don't go to the next track when the player died */
		*wantstat = pl_stop;
		nextstart = 0;
	} else {
		syslog( LOG_DEBUG, "player %d terminated", oldpid );
	}

	/* cleanup things I remembered */
	if( curtrack ){
		if( ! died_unexpected ){
			history_add(curtrack, curuid );
		}
		track_free( curtrack );
		curtrack = NULL;
		curuid = 0;
	}
	died_unexpected = 0;

	/* send broadcasts for what is detectable from here */
	if( last != curstat && 
			curstat == pl_stop && 
			curstat == *wantstat && 
			player_func_stop )
		(*player_func_stop)();

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
			/* give the main child the chance to cleanup
			 * itself and wakeup the whole group */
			kill( curpid, SIGTERM );
			kill( -curpid, SIGCONT );

		} else if( i < 4 ){
			struct timeval tv;

			/* did not terminate yet, try to kill again and
			 * give it some time */
			kill( -curpid, SIGCONT );
			kill( -curpid, SIGTERM );

			tv.tv_sec = 0;
			tv.tv_usec = 1000;
			select( 0, NULL, NULL, NULL, &tv );

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

	/* queue */
	while( NULL != (q = queue_fetch())){
		curtrack = queue_track(q);
		curuid = q->uid;
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

	/* get next track */
	getnext();
	if( NULL == curtrack )
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
		char fname[MAXPATHLEN];

		if( 0 > (fd = open( "/dev/null", O_RDWR, 0700 ))){
			syslog( LOG_ERR, "cannot open /dev/null: %m" );
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

		track_mkpath(fname, MAXPATHLEN, curtrack);

		syslog( LOG_DEBUG, "starting %s %s", opt_player, fname );
		execlp( opt_player, opt_player, fname, NULL );

		syslog( LOG_ERR, "exec of player failed: %m");
		exit( -1 );

	}
	
	/* parent */
	curstat = pl_play;
	curpid = pid;

	if( player_func_newtrack )
		(*player_func_newtrack)();

	/* wait a little to ensure waitpid works for this child */
	while( 0 > waitpid( -pid, NULL, WNOHANG | WUNTRACED) 
			&& errno == ECHILD );


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
	if( curpid )
		return 0;

	return nextstart;
}


t_playerror player_pause( void )
{
	t_playstatus wantstat = pl_pause;
	int i;

	nextstart = 0;
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

	nextstart = 0;
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

	if( wantstat != pl_play )
		nextstart = 0;

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


