#ifndef _TRACK_H
#define _TRACK_H

#include <db.h>

typedef struct _t_track {
	int id;
	int albumid;
	int albumnr;
	char *title;
	int artistid;
	int duration;
	char *fname;
	int _refs;
	unsigned int lastplay;
	union {
		unsigned int any;
		struct {
			unsigned int title:1;
			unsigned int artistid:1;
			unsigned int lastplay:1;
		} m;
	} modified;
} t_track;

typedef void (*t_random_func)( void );

/* TODO: do not assume the db.h iterator for tracks */
#define it_track it_db
#define it_track_begin(x)	((t_track*)it_db_begin(x))
#define it_track_cur(x)		((t_track*)it_db_cur(x))
#define it_track_next(x)	((t_track*)it_db_next(x))
#define it_track_done(x)	it_db_done(x)

t_track *track_use( t_track *t );
void track_free( t_track *t );

int track_settitle( t_track *t, const char *title );
int track_setartist( t_track *t, int artistid );
int track_setrandom( t_track *t, int radnom );
int track_setlastplay( t_track *t, int lastplay );
int track_save( t_track *t );

int tracks( void );
t_track *track_get( int id );
// TODO: t_track *track_getalbumnr( int albumid, int albumnr );

it_track *tracks_albumid( int albumid );
it_track *tracks_artistid( int artistid );
it_db *tracks_search( const char *substr );
// TODO: it_db *tracks_searchf( const char *filter );


int random_setfilter( const char *filt );
int random_filterstat( void );
const char *random_filter( void );
it_track *random_top( int num );
t_track *random_fetch( void );

extern t_random_func random_func_filter;

#endif
