#ifndef _COMMONDB_TRACK_H
#define _COMMONDB_TRACK_H

#include "dudldb.h"
#include "parseexpr.h"
#include "artist.h"
#include "album.h"

typedef struct _t_track {
	int id;
	t_album *album;
	int albumnr;
	char *title;
	t_artist *artist;
	int duration;
	char *fname;
	int _refs;
	unsigned int lastplay;
} t_track;

#define it_track it_db
#define it_track_begin(x)	((t_track*)it_db_begin(x))
#define it_track_cur(x)		((t_track*)it_db_cur(x))
#define it_track_next(x)	((t_track*)it_db_next(x))
#define it_track_done(x)	it_db_done(x)

t_track *track_use( t_track *t );
void track_free( t_track *t );

int track_mkpath( char *buf, int len, t_track *t );
int track_exists( t_track *t );

int track_setname( int trackid, const char *title );
int track_setartist( int trackid, int artistid );

int tracks( void );
int track_id( int album_id, int num );
t_track *track_get( int id );

it_track *tracks_albumid( int albumid );
it_track *tracks_artistid( int artistid );
it_track *tracks_search( const char *substr );
it_track *tracks_searchf( expr *filter );


#endif
