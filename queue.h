#ifndef _QUEUE_H
#define _QUEUE_H

#include "track.h"


typedef struct _t_queue {
	int id;
	t_track *_track;
	int uid;
	time_t queued;
} t_queue;


#define it_queue it_db
#define it_queue_begin(x)	((t_queue*)it_db_begin(x))
#define it_queue_cur(x)		((t_queue*)it_db_cur(x))
#define it_queue_next(x)	((t_queue*)it_db_next(x))
#define it_queue_done(x)	it_db_done(x)


t_queue *queue_fetch( void );
void queue_free( t_queue *q );

t_track *queue_track( t_queue *q );

int queue_add( int trackid, int uid );
int queue_del( int queueid );
int queue_clear( void );

it_queue *queue_list( void );

#endif
