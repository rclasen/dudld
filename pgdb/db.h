#ifndef _PGDB_DB_H
#define _PGDB_DB_H

#include <postgresql/libpq-fe.h>

#include <db.h>

typedef void *(*db_convert)( PGresult *res, int tup );

typedef struct {
	PGresult *res;
	db_convert conv;
	int tuple;
} _it_db;

const char *db_errstr( void );

PGresult *db_query( char *query );
_it_db *db_iterate( char *query, db_convert func );

char *db_escape( const char *in );

int pgint( PGresult *res, int tup, int field );
int pgbool( PGresult *res, int tup, int field );
char *pgstring( PGresult *res, int tup, int field );

#endif
