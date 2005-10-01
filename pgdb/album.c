#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <syslog.h>

// TODO: do not use opt directly
#include <config.h>
#include <opt.h>
#include "album.h"
#include "artist.h"






#define GETFIELD(var,field,gofail) \
	if( -1 == (var = PQfnumber(res, field ))){\
		syslog( LOG_ERR, "missing album data: %s", field ); \
		goto gofail; \
	}

t_album *album_convert( PGresult *res, int tup )
{
	t_album *t;
	int f;

	if( ! res )
		return NULL;

	/* this is checked by PQgetvalue, too. Checking this in advance
	 * makes the error handling easier. */
	if( tup >= PQntuples(res) )
		return NULL;

	if( NULL == (t = malloc(sizeof(t_album))))
		return NULL;


	GETFIELD(f,"album_id", clean1 );
	t->id = pgint(res, tup, f );

	GETFIELD(f,"album_name", clean1 );
	if( NULL == (t->album = pgstring(res, tup, f)))
		goto clean1;

	if( NULL == (t->artist = artist_convert_album( res, tup )))
		goto clean2;

	return t;

clean2:
	free(t->album);

clean1:
	free(t);

	return NULL;
}

void album_free( t_album *t )
{
	artist_free( t->artist );
	free( t->album );
	free( t );
}


int album_setname( t_album *t, const char *name )
{
	PGresult *res;
	char *n;

	if( NULL == (n = db_escape(name)))
		return -1;

	res = db_query( "UPDATE mus_album SET album = '%s' "
			"WHERE id = %d", n, t->id );
	free(n);
	if( NULL == res ||  PGRES_COMMAND_OK != PQresultStatus(res)){
		syslog( LOG_ERR, "album_setname: %s", db_errstr());
		PQclear(res);
		return -1;
	}

	PQclear(res);

	if( NULL == (n = strdup(name)))
		return -1;

	free(t->album);
	t->album = n;
	return 0;
}

int album_setartist( t_album *t, int artistid )
{
	PGresult *res;

	res = db_query( "UPDATE mus_album SET artist_id = %d "
			"WHERE id = %d", artistid, t->id );
	if( NULL == res ||  PGRES_COMMAND_OK != PQresultStatus(res)){
		syslog( LOG_ERR, "album_setartist: %s", db_errstr());
		PQclear(res);
		return -1;
	}

	PQclear(res);

	artist_free(t->artist);
	t->artist = artist_get(artistid);
	return 0;
}

int album_save( t_album *t )
{
	(void)t;
	return 0;
}

t_album *album_get( int id )
{
	PGresult *res;
	t_album *t;

	res = db_query( "SELECT * FROM mserv_album WHERE album_id = %d", id );
	if( NULL == res ||  PGRES_TUPLES_OK != PQresultStatus(res)){
		syslog( LOG_ERR, "album_get: %s", db_errstr());
		PQclear(res);
		return NULL;
	}

	t = album_convert( res, 0 );
	PQclear( res );

	return t;
}

it_album *albums_artistid( int artistid )
{
	return db_iterate( (db_convert)album_convert, "SELECT * "
			"FROM mserv_album "
			"WHERE album_artist_id = %d "
			"ORDER BY LOWER(album_name)", artistid);
}

it_album *albums_list( void )
{
	return db_iterate( (db_convert)album_convert, "SELECT * "
			"FROM mserv_album "
			"ORDER BY LOWER(album_artist_name), LOWER(album_name)");
}

it_album *albums_search( const char *substr )
{
	char *str;
	it_db *it;

	if( NULL == (str = db_escape( substr )))
		return NULL;

	it = db_iterate( (db_convert)album_convert, "SELECT * "
			"FROM mserv_album "
			"WHERE LOWER(album_name) LIKE LOWER('%%%s%%') "
			"ORDER BY LOWER(album_artist_name), LOWER(album_name)", str );
	free(str);
	return it;
}


