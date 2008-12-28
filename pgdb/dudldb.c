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
#include <ctype.h>
#include <stdarg.h>

#include <config.h>
#include <opt.h>
#include "dudldb.h"

static PGconn *dbcon = NULL;
static db_opened_cb opened_cb = NULL;

#define BUFLENQUERY 2048

#define DBVER 4

static int addopt( char *buffer, const char *opt, const char *val )
{
	if( ! val || ! *val )
		return 0;

	return sprintf( buffer, "%s='%s' ", opt, val );
}

static int db_conn( void )
{
	char buffer[1024];
	int len = 0;
	static int inprogress = 0;
	PGresult *res;

	/* avoid endless recursion in case callback function causes reconnect */
	if( inprogress )
		return 0;
	inprogress++;

	db_done();

	*buffer = 0;

	len += addopt( buffer +len, "host", opt_db_host );
	len += addopt( buffer +len, "port", opt_db_port );
	len += addopt( buffer +len, "dbname", opt_db_name );
	len += addopt( buffer +len, "user", opt_db_user );
	len += addopt( buffer +len, "password", opt_db_pass );

	dbcon = PQconnectdb( buffer );
	if( NULL == dbcon || CONNECTION_OK != PQstatus(dbcon)){
		syslog( LOG_ERR, "db_conn failed: %s", PQerrorMessage(dbcon));
		goto clean1;
	}

	if( NULL == (res = db_query( "SELECT ver "
			"FROM dbver "
			"WHERE item = 'schema'")))
		goto clean1;

	if( PGRES_TUPLES_OK !=  PQresultStatus(res) ){
		syslog( LOG_ERR, "db_conn(dbver): %s", db_errstr() );
		goto clean2;
	}
	if( PQntuples(res) != 1 ){
		syslog( LOG_ERR, "db_conn(dbver): couldn't determine dbver, "
				"found %d rows", PQntuples(res));
		goto clean2;
	}
	syslog( LOG_DEBUG, "DB Version: %d", pgint(res,0,0));

	if( pgint(res, 0, 0 ) != DBVER ){
		syslog( LOG_ERR, "db_conn: invalid DB Version %d - need %d", 
				pgint(res,0,0), DBVER);
		goto clean2;
	}
	PQclear(res);

	if( opened_cb )
		(*opened_cb)();

	inprogress--;
	return 0;

clean2:
	PQclear(res);

clean1:
	PQfinish(dbcon);
	dbcon = NULL;
	inprogress--;
	return 1;
}

const char *db_errstr( void )
{
	if( ! dbcon )
		return "no database handle open";

	return PQerrorMessage(dbcon);
}

/*
 * wrapper to reconnect to database, when connection was lost
 */

static PGresult *db_vquery( char *query, va_list ap )
{
	char buf[BUFLENQUERY];
	int n;
	PGresult *res = NULL;

	n = vsnprintf( buf, BUFLENQUERY, query, ap );
	if( n < 0 || n > BUFLENQUERY ){
		syslog( LOG_ERR, "query buffer too small, "
				"increase BUFLENQUERY" );
		return NULL;
	}

	syslog( LOG_DEBUG, "db_vquery(%s)", buf );

	/* we have a connecteio? try the query */
	if( dbcon ){
		res = PQexec( dbcon, buf );
	}

	if( PQstatus(dbcon) == CONNECTION_OK )
		return res;

	/* reconnect and retry */
	PQclear( res );
	res = NULL;

	if( db_conn() )
		return NULL;

	return PQexec( dbcon, buf );
}

PGresult *db_query( char *query, ... )
{
	PGresult *res;
	va_list ap;

	va_start(ap,query);
	res = db_vquery( query, ap );
	va_end( ap );

	return res;
}

_it_db *db_iterate( db_convert func, char *query, ... )
{
	va_list ap;
	PGresult *res = NULL;
	_it_db *it;

	if( NULL == func )
		return NULL;

	va_start(ap,query);
	res = db_vquery( query, ap );
	va_end( ap );

	if( res == NULL || PQresultStatus(res) != PGRES_TUPLES_OK ){
		syslog( LOG_ERR, "query >%s< failed: %s", query, PQerrorMessage(dbcon));
		PQclear(res);
		return NULL;
	}

	if( NULL == (it = malloc(sizeof(_it_db)))){
		PQclear(res);
		return NULL;
	}

	it->res = res;
	it->conv = func;
	it->tuple = 0;

	return it;
}

char *db_escape( const char *in )
{
	int len;
	const char *p;
	char *q, *esc;

	/* return a simple copy, if there is nothing to escape */
	if( NULL == (p = strchr(in, '\'')))
		return strdup(in);

	/* get length of string to allocate */
	len = strlen(in);
	while( NULL != (p = strchr(p, '\'' ) )){
		len++;
		p++;
	}

	if( NULL == (esc = malloc(len+1)))
		return NULL;

	/* copy string. a single ' is duplicated to escape it */
	q = esc;
	for( p = in; p && *p; ++p ){
		*q++ = *p;
		if( *p == '\'' ){
			*q++ = '\'';
		}
	}
	*q = 0;

	return esc;
}


int db_table_exists( char *table )
{
	PGresult *res;
	int found = 0;

	res = db_query( "SELECT relname "
			"FROM pg_class "
			"WHERE relkind = 'r' "
				"AND relname = '%s'", table );
	if( ! res || PGRES_TUPLES_OK !=  PQresultStatus(res) ){
		syslog( LOG_ERR, "table_exists: %s", db_errstr() );
		PQclear(res);
		return -1;
	}

	if( PQntuples(res))
		found++;

	PQclear(res);
	return found;
}

int pgint( PGresult *res, int tup, int field )
{
	return strtol(PQgetvalue( res, tup, field ), NULL, 10);
}

unsigned int pguint( PGresult *res, int tup, int field )
{
	return strtoul(PQgetvalue( res, tup, field ), NULL, 10);
}

gint64 pgint64( PGresult *res, int tup, int field )
{
	return g_ascii_strtoll(PQgetvalue( res, tup, field ), NULL, 10);
}

guint64 pguint64( PGresult *res, int tup, int field )
{
	return g_ascii_strtoull(PQgetvalue( res, tup, field ), NULL, 10);
}

double pgdouble( PGresult *res, int tup, int field )
{
	return strtod(PQgetvalue( res, tup, field ), NULL);
}

int pgbool( PGresult *res, int tup, int field )
{
	return 0 != strcmp(PQgetvalue( res, tup, field ),"t");

}

char *pgstring( PGresult *res, int tup, int field )
{
	char *c, *e;

	if( NULL == (c = strdup(PQgetvalue( res, tup, field ))))
		return NULL;

	e = c + strlen(c);
	while( --e > c && isspace(*e))
		*e = 0;

	return c;
}



/************************************************************
 * public interface
 */
int db_init( db_opened_cb cbfunc )
{
	opened_cb = cbfunc;
	return db_conn();
}

void db_done( void )
{
	if( dbcon )
		PQfinish( dbcon );
	dbcon = NULL;
}


#define ITDB(x)		((_it_db*)x)

it_db *it_db_begin( it_db *i )
{
	if( ! i )
		return NULL;

	ITDB(i)->tuple = 0;
	return it_db_cur(i);
}

it_db *it_db_cur( it_db *i )
{
	if( ! i )
		return NULL;

	return (*ITDB(i)->conv)( ITDB(i)->res, ITDB(i)->tuple);
}

it_db *it_db_next( it_db *i )
{
	if( ! i )
		return NULL;

	ITDB(i)->tuple++;
	return it_db_cur(i);
}

void it_db_done( it_db *i )
{
	if( ! i )
		return;

	PQclear( ITDB(i)->res );
	free( i );
}
