#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <syslog.h>

// TODO: do not use opt directly
#include <opt.h>
#include <commondb/artist.h>
#include "dudldb.h"






#define GETFIELD(var,field,gofail) \
	if( -1 == (var = PQfnumber(res, field ))){\
		syslog( LOG_ERR, "missing artist data: %s", field ); \
		goto gofail; \
	}

static t_artist *artist_convert( PGresult *res, int tup )
{
	t_artist *t;
	int f;

	if( ! res )
		return NULL;

	/* this is checked by PQgetvalue, too. Checking this in advance
	 * makes the error handling easier. */
	if( tup >= PQntuples(res) )
		return NULL;

	if( NULL == (t = malloc(sizeof(t_artist))))
		return NULL;


	GETFIELD(f,"id", clean1 );
	t->id = pgint(res, tup, f );

	GETFIELD(f,"nname", clean1 );
	if( NULL == (t->artist = pgstring(res, tup, f)))
		goto clean1;

	return t;

clean1:
	free(t);

	return NULL;
}

void artist_free( t_artist *t )
{
	free( t );
}


int artist_setname( t_artist *t, const char *name )
{
	PGresult *res;
	char *n;

	if( NULL == (n = db_escape(name)))
		return -1;

	res = db_query( "UPDATE mus_artist SET nname = '%s' "
			"WHERE id = %d", n, t->id );
	free(n);
	if( NULL == res ||  PGRES_COMMAND_OK != PQresultStatus(res)){
		syslog( LOG_ERR, "artist_setname: %s", db_errstr());
		PQclear(res);
		return -1;
	}

	PQclear(res);

	if( NULL == (n = strdup(name)))
		return -1;

	free(t->artist);
	t->artist = n;
	return 0;
}

int artist_save( t_artist *t )
{
	(void)t;
	return 0;
}

t_artist *artist_get( int id )
{
	PGresult *res;
	t_artist *t;

	res = db_query( "SELECT * FROM mus_artist WHERE id = %d", id );
	if( NULL == res ||  PGRES_TUPLES_OK != PQresultStatus(res)){
		syslog( LOG_ERR, "artist_get: %s", db_errstr());
		PQclear(res);
		return NULL;
	}

	t = artist_convert( res, 0 );
	PQclear( res );

	return t;
}

it_artist *artist_list( void )
{
	return db_iterate( (db_convert)artist_convert, "SELECT * "
			"FROM mus_artist");
}

it_artist *artist_search( const char *substr )
{
	char *str;
	it_db *it;

	if( NULL == (str = db_escape( substr )))
		return NULL;

	it = db_iterate( (db_convert)artist_convert, "SELECT * "
			"FROM mus_artist "
			"WHERE LOWER(nname) LIKE LOWER('%%%s%%')", str );
	free(str);
	return it;
}

