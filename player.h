#ifndef _PLAYER_H
#define _PLAYER_H

#include "track.h"

typedef enum {
	pl_stop,
	/* all below states are substates of pl_play */
	pl_play,
	pl_pause,
	pl_gap,
	pl_gpause,
} t_playstate;

typedef enum {
	PE_OK = 0,
	PE_SYS,
	PE_NOTHING,
	PE_BUSY,
	PE_NOSUP,
	PE_FAIL,
} t_playerror;

typedef void (*t_player_func_update)( void );

/* callbacks */
extern t_player_func_update player_func_newtrack;
extern t_player_func_update player_func_pause;
extern t_player_func_update player_func_resume;
extern t_player_func_update player_func_stop;

t_playstate player_status( void );
t_track *player_track( void );

t_playerror player_start( void );
t_playerror player_stop( void );
t_playerror player_next( void );
t_playerror player_prev( void );
t_playerror player_pause( void );
t_playerror player_resume( void );

t_playerror player_check( void );
time_t player_wakeuptime( void );
#endif
