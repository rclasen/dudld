#ifndef _ARTIST_H
#define _ARTIST_H

#include "dudldb.h"

typedef struct _t_artist {
	int id;
	char *artist;
} t_artist;

#define it_artist it_db
#define it_artist_begin(x)	((t_artist*)it_db_begin(x))
#define it_artist_cur(x)	((t_artist*)it_db_cur(x))
#define it_artist_next(x)	((t_artist*)it_db_next(x))
#define it_artist_done(x)	it_db_done(x)

void artist_free( t_artist *t );

t_artist *artist_get( int id );

it_artist *artist_list( void );
it_artist *artist_search( const char *substr );

int artist_setname( t_artist *t, const char *name );
int artist_save( t_artist *t );


#endif
