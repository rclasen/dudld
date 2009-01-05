/*
 * Copyright (c) 2008 Rainer Clasen
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */


#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include <config.h>
#include <commondb/sfilter.h>
#include "dudldb.h"

#define GETFIELD(var,field,gofail) \
	if( -1 == (var = PQfnumber(res, field ))){\
		syslog( LOG_ERR, "missing sfilter data: %s", field ); \
		goto gofail; \
	}

static t_sfilter *sfilter_convert( PGresult *res, int tup )
{
	t_sfilter *h;
	int f;

	if( ! res )
		return NULL;

	if( tup >= PQntuples(res) )
		return NULL;

	if( NULL == (h = malloc(sizeof(t_sfilter))))
		return NULL;
	memset( h, 0, sizeof(t_sfilter));

	GETFIELD(f,"id", clean1 );
	h->id = pgint(res, tup, f);

	GETFIELD(f,"name", clean1 );
	if( NULL == ( h->name = pgstring(res, tup, f )))
		goto clean1;

	GETFIELD(f,"filter", clean1 );
	if( NULL == ( h->filter = pgstring(res, tup, f )))
		goto clean2;

	return h;

clean2:
	free(h->name);

clean1:
	free(h);

	return NULL;
}


void sfilter_free( t_sfilter *t )
{
	if( ! t )
		return;

	free(t->name);
	free(t->filter);
	free(t);
}

t_sfilter *sfilter_get( int id )
{
	PGresult *res;
	t_sfilter *t;

	res = db_query( "SELECT id, name, filter "
			"FROM juke_sfilter WHERE id = %d",id);
	if( ! res || PQresultStatus(res) != PGRES_TUPLES_OK ){
		syslog( LOG_ERR, "sfilter_get: %s", db_errstr());
		PQclear(res);
		return NULL;
	}

	t = sfilter_convert(res, 0 );
	PQclear(res);

	return t;
}

int sfilter_id( const char *name )
{
	PGresult *res;
	int id;
	char *esc;

	if( NULL == (esc = db_escape(name)))
		return -1;

	res = db_query( "SELECT id FROM juke_sfilter "
			"WHERE name = '%s'", esc);
	free(esc);
	if( ! res || PQresultStatus(res) != PGRES_TUPLES_OK ){
		syslog( LOG_ERR, "sfilter_id: %s", db_errstr());
		PQclear(res);
		return -1;
	}

	if( PQntuples(res) != 1 ){
		PQclear(res);
		return -1;
	}

	id = pgint( res, 0, 0);
	PQclear(res);

	return id;
}


it_sfilter *sfilters_list( void )
{
	return db_iterate( (db_convert)sfilter_convert, 
			"SELECT id, name, filter "
			"FROM juke_sfilter "
			"ORDER BY LOWER(name)" );
}

int sfilter_add( const char *name )
{
	PGresult *res;
	char *esc;
	int id;

	res = db_query( "SELECT nextval('juke_sfilter_id_seq')" );
	if( !res || PQresultStatus(res) != PGRES_TUPLES_OK ){
		syslog( LOG_ERR, "sfilter_add: %s", db_errstr());
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

	res = db_query( "INSERT INTO juke_sfilter(id, name) "
			"VALUES( %d, '%s')", id, esc );
	free(esc);
	if( ! res || PQresultStatus(res) != PGRES_COMMAND_OK ){
		syslog( LOG_ERR, "sfilter_add: %s", db_errstr());
		PQclear(res);
		return -1;
	}

	PQclear(res);

	return id;
}

int sfilter_del( int id )
{
	PGresult *res;

	res = db_query("DELETE FROM juke_sfilter WHERE id = %d", id);
	if( ! res || PQresultStatus(res) != PGRES_COMMAND_OK ){
		syslog( LOG_ERR, "sfilter_del: %s", db_errstr());
		PQclear(res);
		return -1;
	}

	PQclear(res);
	return 0;
}

int sfilter_setname( int id, const char *name )
{
	PGresult *res;
	char *esc;

	if( NULL == (esc = db_escape( name )))
		return -1;

	res = db_query( "UPDATE juke_sfilter SET name ='%s' "
			"WHERE id = %d", esc, id );
	free(esc);

	if( ! res || PQresultStatus(res) != PGRES_COMMAND_OK ){
		syslog( LOG_ERR, "sfilter_setname: %s", db_errstr());
		PQclear(res);
		return -1;
	}

	PQclear(res);

	return 0;
}

int sfilter_setfilter( int id, const char *filter )
{
	PGresult *res;
	char *esc;

	if( NULL == (esc = db_escape( filter )))
		return -1;

	res = db_query( "UPDATE juke_sfilter SET filter ='%s' "
			"WHERE id = %d", esc, id );
	free(esc);

	if( ! res || PQresultStatus(res) != PGRES_COMMAND_OK ){
		syslog( LOG_ERR, "sfilter_setfilter: %s", db_errstr());
		PQclear(res);
		return -1;
	}

	PQclear(res);

	return 0;
}



