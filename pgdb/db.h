#ifndef _PGDB_DB_H
#define _PGDB_DB_H

#include <postgresql/libpq-fe.h>

#include <db.h>

typedef void *(*db_convert)( PGresult *res, int tup );

typedef struct _it_db {
	PGresult *res;
	db_convert conv;
	int tuple;
} it_db;

PGresult *db_query( char *query );
it_db *db_iterate( char *query, db_convert func );

#endif
