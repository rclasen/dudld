#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <syslog.h>

#include <opt.h>
#include <pgdb/db.h>
#include <album.h>






#define GETFIELD(var,field,gofail) \
	if( -1 == (var = PQfnumber(res, field ))){\
		syslog( LOG_ERR, "missing album data: %s", field ); \
		goto gofail; \
	}

static t_album *album_convert( PGresult *res, int tup )
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


	GETFIELD(f,"id", clean1 );
	t->id = pgint(res, tup, f );

	GETFIELD(f,"artist_id", clean1 );
	t->artistid = pgint(res, tup, f);

	GETFIELD(f,"album", clean1 );
	if( NULL == (t->album = pgstring(res, tup, f)))
		goto clean1;

	return t;

clean1:
	free(t);

	return NULL;
}

void album_free( t_album *t )
{
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

	t->artistid = artistid;
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

	res = db_query( "SELECT * FROM mus_album WHERE id = %d", id );
	if( NULL == res ||  PGRES_TUPLES_OK != PQresultStatus(res)){
		syslog( LOG_ERR, "album_get: %s", db_errstr());
		PQclear(res);
		return NULL;
	}

	t = album_convert( res, 0 );
	PQclear( res );

	return t;
}

it_album *album_list( void )
{
	return db_iterate( (db_convert)album_convert, "SELECT * "
			"FROM mus_album");
}

it_album *album_search( const char *substr )
{
	char *str;
	it_db *it;

	if( NULL == (str = db_escape( substr )))
		return NULL;

	it = db_iterate( (db_convert)album_convert, "SELECT * "
			"FROM mus_album "
			"WHERE LOWER(album) LIKE LOWER('%%%s%%')", str );
	free(str);
	return it;
}


