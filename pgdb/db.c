
#include <stdlib.h>
#include <string.h>

#include "../opt.h"
#include "db.h"

static PGconn *dbcon = NULL;


static void addopt( char *buffer, const char *opt, const char *val )
{
	if( ! val || ! *val )
		return;

	if( *buffer )
		strcat( buffer, "," );

	strcat( buffer, opt );
	strcat( buffer, "=" );
	strcat( buffer, val );
}

static int db_conn( void )
{
	char buffer[1024];

	db_done();

	*buffer = 0;

	addopt( buffer, "host", opt_db_host );
	addopt( buffer, "port", opt_db_port );
	addopt( buffer, "dbname", opt_db_name );
	addopt( buffer, "user", opt_db_user );
	addopt( buffer, "password", opt_db_pass );

	if( NULL == (dbcon = PQconnectdb( buffer )))
		// TODO: log connect failure?
		return 1;

	return 0;
}

/*
 * wrapper to reconnect to database, when connection was lost
 */
PGresult *db_query( char *query )
{
	PGresult *res = NULL;

	/* we have a connecteio? try the query */
	if( dbcon ){
		res = PQexec( dbcon, query );
		if( res != NULL && PQresultStatus(res) == PGRES_FATAL_ERROR){
			PQclear( res );
			res = NULL;
		}
	}

	/* first query succeeded */
	if( res != NULL )
		return res;

	// TODO: log query failure

	/* reconnect and retry */
	if( db_conn() )
		return NULL;

	return PQexec( dbcon, query );
}

it_db *db_iterate( char *query, db_convert func )
{
	PGresult *res = NULL;
	it_db *it;

	if( NULL == func )
		return NULL;

	if( NULL == (res = db_query( query )))
		return NULL;

	if( NULL == (it = malloc(sizeof(it_db)))){
		PQclear(res);
		return NULL;
	}

	it->res = res;
	it->conv = func;
	it->tuple = 0;

	return it;
}



/************************************************************
 * public interface
 */
int db_init( void )
{
	return db_conn();
}

void db_done( void )
{
	if( dbcon )
		PQfinish( dbcon );
	dbcon = NULL;
}


#define ITDB(x)		((it_db*)x)

void *it_db_begin( void *i )
{
	if( ! i )
		return NULL;

	ITDB(i)->tuple = 0;
	return it_db_cur(i);
}

void *it_db_cur( void *i )
{
	if( ! i )
		return NULL;

	return (*ITDB(i)->conv)( ITDB(i)->res, ITDB(i)->tuple);
}

void *it_db_next( void *i )
{
	if( ! i )
		return NULL;

	ITDB(i)->tuple++;
	return it_db_cur(i);
}

void it_db_done( void *i )
{
	if( ! i )
		return;

	PQclear( ITDB(i)->res );
	free( i );
}
