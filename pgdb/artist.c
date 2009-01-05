/*
 * Copyright (c) 2008 Rainer Clasen
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <syslog.h>

#include <config.h>
#include "artist.h"



typedef struct _t_artist_col {
	char	*id;
	char	*artist;
} t_artist_col;

static t_artist_col title_col = {
	.id = "artist_id",
	.artist = "artist_name",
};

static t_artist_col album_col = {
	.id = "album_artist_id",
	.artist = "album_artist_name",
};


#define GETFIELD(var,field,gofail) \
	if( -1 == (var = PQfnumber(res, field ))){\
		syslog( LOG_ERR, "missing artist data: %s", field ); \
		goto gofail; \
	}


static t_artist *artist_convert( PGresult *res, int tup, t_artist_col *col )
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
	memset( t, 0, sizeof(t_artist));


	GETFIELD(f, col->id, clean1 );
	t->id = pgint(res, tup, f );

	GETFIELD(f, col->artist, clean1 );
	if( NULL == (t->artist = pgstring(res, tup, f)))
		goto clean1;

	return t;

clean1:
	free(t);

	return NULL;
}

t_artist *artist_convert_title( PGresult *res, int tup )
{
	return artist_convert( res, tup, &title_col );
}

t_artist *artist_convert_album( PGresult *res, int tup )
{
	return artist_convert( res, tup, &album_col );
}

void artist_free( t_artist *t )
{
	if( ! t )
		return;

	free( t->artist );
	free( t );
}


int artist_setname( int artistid, const char *name )
{
	PGresult *res;
	char *n;

	if( NULL == (n = db_escape(name)))
		return -1;

	res = db_query( "UPDATE mus_artist SET nname = '%s' "
			"WHERE id = %d", n, artistid );
	free(n);
	if( NULL == res ||  PGRES_COMMAND_OK != PQresultStatus(res)){
		syslog( LOG_ERR, "artist_setname: %s", db_errstr());
		PQclear(res);
		return -1;
	}

	PQclear(res);

	return 0;
}

int artist_merge( int fromid, int toid )
{
	PGresult *res;

	res = db_query( "BEGIN" );
	if( NULL == res ||  PGRES_COMMAND_OK != PQresultStatus(res))
		goto clean1;

	res = db_query( "UPDATE stor_file "
			"SET artist_id = %d "
			"WHERE artist_id = %d",
			toid, fromid );
	if( NULL == res ||  PGRES_COMMAND_OK != PQresultStatus(res))
		goto clean2;

	res = db_query( "UPDATE mus_album "
			"SET artist_id = %d "
			"WHERE artist_id = %d",
			toid, fromid );
	if( NULL == res ||  PGRES_COMMAND_OK != PQresultStatus(res))
		goto clean2;

	res = db_query( "DELETE FROM mus_artist "
			"WHERE id = %d",
			fromid );
	if( NULL == res ||  PGRES_COMMAND_OK != PQresultStatus(res))
		goto clean2;

	res = db_query( "COMMIT" );
	if( NULL == res ||  PGRES_COMMAND_OK != PQresultStatus(res))
		goto clean2;


	return 0;

clean1:
	syslog( LOG_ERR, "artist_merge: %s", db_errstr());
	PQclear(res);
	return -1;

clean2:
	syslog( LOG_ERR, "artist_merge: %s", db_errstr());
	PQclear(res);

	res = db_query( "ROLLBACK" );
	PQclear(res);
	return 1;
}

t_artist *artist_get( int id )
{
	PGresult *res;
	t_artist *t;

	res = db_query( "SELECT * FROM mserv_artist WHERE artist_id = %d", id );
	if( NULL == res ||  PGRES_TUPLES_OK != PQresultStatus(res)){
		syslog( LOG_ERR, "artist_get: %s", db_errstr());
		PQclear(res);
		return NULL;
	}

	t = artist_convert( res, 0, &title_col );
	PQclear( res );

	return t;
}

it_artist *artists_list( void )
{
	return db_iterate( (db_convert)artist_convert_title, "SELECT * "
			"FROM mserv_artist "
			"ORDER BY LOWER(artist_name)");
}

it_artist *artists_tag( int tid )
{
	return db_iterate( (db_convert)artist_convert_title, "SELECT * "
			"FROM mserv_artist a "
			"WHERE a.artist_id IN ( "
				"SELECT f.artist_id FROM stor_file f "
				"INNER JOIN mserv_filetag t "
				"ON f.id = t.file_id "
				"WHERE t.tag_id = %d ) "
			"ORDER BY LOWER(artist_name)", tid);
}

it_artist *artists_search( const char *substr )
{
	char *str;
	it_db *it;

	if( NULL == (str = db_escape( substr )))
		return NULL;

	it = db_iterate( (db_convert)artist_convert_title, "SELECT * "
			"FROM mserv_artist "
			"WHERE LOWER(artist_name) LIKE LOWER('%%%s%%') "
			"ORDER BY LOWER(artist_name)", str );
	free(str);
	return it;
}

int artist_add( const char *name )
{
	PGresult *res;
	char *esc;
	int id;

	res = db_query( "SELECT nextval('mus_artist_id_seq')" );
	if( !res || PQresultStatus(res) != PGRES_TUPLES_OK ){
		syslog( LOG_ERR, "artist_add: %s", db_errstr());
		PQclear(res);
		return -1;
	}

	if( 0 > (id = pgint(res,0,0))){
		PQclear(res);
		return -1;
	}
	PQclear(res);

	if( NULL == (esc = db_escape(name)))
		return -1;

	res = db_query( "INSERT INTO mus_artist(id, nname) "
			"VALUES( %d, '%s')", id, esc );
	free(esc);
	if( ! res || PQresultStatus(res) != PGRES_COMMAND_OK ){
		syslog( LOG_ERR, "artist_add: %s", db_errstr());
		PQclear(res);
		return -1;
	}

	PQclear(res);

	return id;
}

int artist_del( int artistid )
{
	PGresult *res;

	res = db_query("DELETE FROM mus_artist WHERE id = %d", artistid);
	if( ! res || PQresultStatus(res) != PGRES_COMMAND_OK ){
		syslog( LOG_ERR, "artist_del: %s", db_errstr());
		PQclear(res);
		return -1;
	}

	PQclear(res);
	return 0;
}

