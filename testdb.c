
#include <stdio.h>
#include <time.h>

#include "db.h"
#include "track.h"

int main( int argc, char **argv )
{
	t_track *t;
	it_track *it;

	(void)argc;
	(void)argv;

	t = track_get( 6551 );
	if( t ){
		printf( "%2d %s\n", t->albumnr, t->title );
		track_setlastplay(t, time(NULL));
		track_save(t);
		track_free( t );
	}


	it = tracks_albumid( 542 );
	for( t = it_track_begin(it); t; t = it_track_next(it) ){
		printf( "%2d %s\n", t->albumnr, t->title );
		track_free(t);
	}
	it_track_done( it );


	it = tracks_search( "n't" );
	for( t = it_track_begin(it); t; t = it_track_next(it) ){
		printf( "%2d %s\n", t->albumnr, t->title );
		track_free(t);
	}
	it_track_done( it );

	return 0;
}
