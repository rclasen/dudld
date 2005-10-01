#ifndef _COMMONDB_HISTORY_H
#define _COMMONDB_HISTORY_H

#include <time.h>

#include "track.h"
#include "user.h"

typedef struct _t_history {
	t_track *track;
	time_t played;
	t_user *user;
} t_history;

int history_add( t_track *t, int uid );

#define it_history it_db
#define it_history_begin(x)	((t_history*)it_db_begin(x))
#define it_history_cur(x)	((t_history*)it_db_cur(x))
#define it_history_next(x)	((t_history*)it_db_next(x))
#define it_history_done(x)	it_db_done(x)

t_track *history_track( t_history *h );
void history_free( t_history *h );

it_history *history_list( int num );
it_history *history_tracklist( int trackid, int num );


#endif
