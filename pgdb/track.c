#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <syslog.h>

#include <opt.h>
#include <pgdb/db.h>
#include <pgdb/track.h>






#define GETFIELD(var,field,gofail) \
	if( -1 == (var = PQfnumber(res, field ))){\
		syslog( LOG_ERR, "missing track data: %s", field ); \
		goto gofail; \
	}

t_track *track_convert( PGresult *res, int tup )
{
	t_track *t;
	int f;

	if( ! res )
		return NULL;

	/* this is checked by PQgetvalue, too. Checking this in advance
	 * makes the error handling easier. */
	if( tup >= PQntuples(res) )
		return NULL;

	if( NULL == (t = malloc(sizeof(t_track))))
		return NULL;

	t->_refs = 1;
	t->modified.any = 0;

	GETFIELD(f,"id", clean1 );
	t->id = pgint(res, tup, f );

	GETFIELD(f,"album_id", clean1 );
	t->albumid = pgint(res, tup, f);

	GETFIELD(f,"nr", clean1 );
	t->albumnr = pgint(res, tup, f);

	GETFIELD(f,"artist_id", clean1 );
	t->artistid = pgint(res, tup, f);

	t->lastplay = 0;
	if( -1 != (f = PQfnumber( res, "lplay" )))
		t->lastplay = pgint(res, tup, f);

	t->duration = 0;
	if( -1 != (f = PQfnumber( res, "dur" )))
		t->duration = pgint(res, tup, f);

	GETFIELD(f,"filename", clean1 );
	if( NULL == (t->fname = pgstring(res, tup, f)))
		goto clean1;

	GETFIELD(f,"title", clean2 );
	if( NULL == (t->title = pgstring(res, tup, f)))
		goto clean2;

	return t;

clean2:
	free(t->fname);

clean1:
	free(t);

	return NULL;
}

t_track *track_use( t_track *t )
{
	t->_refs ++;
	return t;
}

void track_free( t_track *t )
{
	if( 0 < -- t->_refs)
		return;

	free( t->fname );
	free( t->title );
	free( t );
}


int track_settitle( t_track *t, const char *title )
{
	char *n;

	if( NULL == (n = strdup(title)))
		return 1;

	free(t->title);
	t->title = n;
	t->modified.m.title = 1;
	return 0;
}

int track_setartist( t_track *t, int artistid )
{
	t->artistid = artistid;
	t->modified.m.artistid = 1;
	return 0;
}

static int addcom( char *buffer, int len, int *fields )
{
	if( *fields && len ){
		strcat( buffer, ",");
		(*fields)++;
		return 1;
	}

	return 0;
}

#define SBUFLEN 1024
int track_save( t_track *t )
{
	char buffer[SBUFLEN];
	int len;
	int fields = 0;
	PGresult *res;

	if( ! t->id )
		return 1;

	if( ! t->modified.any )
		return 0;

	len = snprintf( buffer, SBUFLEN, "UPDATE mus_title SET ");
	if( len > SBUFLEN || len < 0 )
		return 1;

	if( t->modified.m.title ){
		char *esc;
		if( NULL == (esc = db_escape(t->title)))
			return 1;

		len += addcom( buffer + len, SBUFLEN - len, &fields );
		len += snprintf( buffer + len, SBUFLEN - len, "title='%s'",
				esc );
		free( esc );
		if( len > SBUFLEN || len < 0 )
			return 1;
	}

	if( t->modified.m.artistid ){
		len += addcom( buffer + len, SBUFLEN - len, &fields );
		len += snprintf( buffer + len, SBUFLEN - len, "artist_id=%d", 
				t->artistid );
		if( len > SBUFLEN || len < 0 )
			return 1;
	}

	snprintf( buffer+len, SBUFLEN - len, " WHERE id = %d", t->id );
	if( len > SBUFLEN || len < 0 )
		return 1;

	res = db_query( "%s", buffer );

	if( ! res || PGRES_COMMAND_OK != PQresultStatus(res)){
		syslog( LOG_ERR, "track_save: %s", db_errstr());
		return 1;
	}

	t->modified.any = 0;
	return 0;
}


t_track *track_get( int id )
{
	PGresult *res;
	t_track *t;

	// TODO: for a single track it is faster to query all three tables
	// seperately

	res = db_query( "SELECT * FROM mserv_track WHERE id = %d", id );
	if( NULL == res ||  PGRES_TUPLES_OK != PQresultStatus(res)){
		syslog( LOG_ERR, "track_get: %s",
				db_errstr());
		PQclear(res);
		return NULL;
	}

	t = track_convert( res, 0 );
	PQclear( res );

	return t;
}

it_track *tracks_albumid( int albumid )
{
	return db_iterate( (db_convert)track_convert, "SELECT * "
			"FROM mserv_track "
			"WHERE  album_id = %d ORDER BY nr", albumid );
}


it_track *tracks_artistid( int artistid )
{
	return db_iterate( (db_convert)track_convert, "SELECT * "
			"FROM mserv_track "
			"WHERE artist_id = %d", artistid );
}


it_track *tracks_search( const char *substr )
{
	char *str;
	it_db *it;

	if( NULL == (str = db_escape( substr )))
		return NULL;

	it = db_iterate( (db_convert)track_convert, "SELECT * "
			"FROM mserv_track "
			"WHERE LOWER(title) LIKE LOWER('%%%s%%')", str );
	free(str);
	return it;
}

int tracks( void )
{
	PGresult *res;
	int num;

	res = db_query( "SELECT count(*) FROM mus_title" );
	if( ! res || PGRES_TUPLES_OK !=  PQresultStatus(res) ){
		syslog( LOG_ERR, "tracks: %s", db_errstr() );
		PQclear(res);
		return -1;
	}

	num = pgint(res, 0, 0 );
	PQclear(res);

	return num;
}

int track_exists( t_track *t )
{
	char buf[MAXPATHLEN];
	PGresult *res;
	int fd;

	/* try to open the file - the easiest way to see, if it is
	 * readable */
	snprintf( buf, MAXPATHLEN, "%s/%s", opt_path_tracks, t->fname );
	if( 0 > (fd = open( buf, O_RDONLY ))){

		res = db_query( "UPDATE mus_title SET available = false "
				"WHERE trackid = %d", t->id );

		if( ! res || PQresultStatus(res) != PGRES_COMMAND_OK )
			syslog( LOG_ERR, "track_exists: %s", db_errstr() );

		PQclear(res);
		return 0;
	}
	
	close(fd);
	return 1;
}

