#ifndef _COMMONDB_ALBUM_H
#define _COMMONDB_ALBUM_H

#include "dudldb.h"
#include "artist.h"

typedef struct _t_album {
	int id;
	char *album;
	t_artist *artist;
} t_album;

#define it_album it_db
#define it_album_begin(x)	((t_album*)it_db_begin(x))
#define it_album_cur(x)		((t_album*)it_db_cur(x))
#define it_album_next(x)	((t_album*)it_db_next(x))
#define it_album_done(x)	it_db_done(x)

void album_free( t_album *t );

t_album *album_get( int id );

it_album *albums_list( void );
it_album *albums_artistid( int artistid );
it_album *albums_search( const char *substr );

int album_setname( t_album *t, const char *name );
int album_setartist( t_album *t, int artistid );
int album_save( t_album *t );

#endif
