#ifndef _PLAYER_H
#define _PLAYER_H

#include "track.h"

typedef enum {
	pl_stop,
	pl_play,
	pl_pause,
} t_playstatus;

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
extern t_player_func_update player_func_random;

t_playstatus player_status( void );
t_track *player_track( void );
int player_gap( void );
t_playerror player_setgap( int gap );
int player_random( void );
t_playerror player_setrandom( int random );

t_playerror player_start( void );
t_playerror player_stop( void );
t_playerror player_next( void );
t_playerror player_prev( void );
t_playerror player_pause( void );
t_playerror player_resume( void );

void player_check( void );
time_t player_wakeuptime( void );
#endif
