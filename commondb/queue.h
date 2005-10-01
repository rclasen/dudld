#ifndef _COMMONDB_QUEUE_H
#define _COMMONDB_QUEUE_H

#include "track.h"
#include "user.h"


typedef struct _t_queue {
	int id;
	t_track *track;
	t_user *user;
	time_t queued;
	int _refs;
} t_queue;

typedef void (*t_queue_func_clear)( void );
typedef void (*t_queue_func_fetch)( t_queue *q );

extern t_queue_func_clear queue_func_clear;
extern t_queue_func_fetch queue_func_add;
extern t_queue_func_fetch queue_func_del;
extern t_queue_func_fetch queue_func_fetch;

#define it_queue it_db
#define it_queue_begin(x)	((t_queue*)it_db_begin(x))
#define it_queue_cur(x)		((t_queue*)it_db_cur(x))
#define it_queue_next(x)	((t_queue*)it_db_next(x))
#define it_queue_done(x)	it_db_done(x)


t_queue *queue_fetch( void );
void queue_free( t_queue *q );
void queue_use( t_queue *q );

t_track *queue_track( t_queue *q );

t_queue *queue_get( int id );
int queue_add( int trackid, int uid );
int queue_del( int queueid, int uid );
int queue_clear( void );

it_queue *queue_list( void );

#endif
