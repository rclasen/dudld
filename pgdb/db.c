
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include <opt.h>
#include <pgdb/db.h>

static PGconn *dbcon = NULL;


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
		PQfinish(dbcon);
		dbcon = NULL;
		return 1;
	}

	return 0;
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

	/* reconnect and retry */
	if( db_conn() )
		return NULL;

	return PQexec( dbcon, query );
}

_it_db *db_iterate( char *query, db_convert func )
{
	PGresult *res = NULL;
	_it_db *it;

	if( NULL == func )
		return NULL;

	if( NULL == (res = db_query( query )))
		return NULL;

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


int pgint( PGresult *res, int tup, int field )
{
	return atoi(PQgetvalue( res, tup, field ));
}

int pgbool( PGresult *res, int tup, int field )
{
	return 0 != strcmp(PQgetvalue( res, tup, field ),"t");

}

char *pgstring( PGresult *res, int tup, int field )
{
	return strdup(PQgetvalue( res, tup, field ));
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
