#ifndef _PGDB_DB_H
#define _PGDB_DB_H

#include <postgresql/libpq-fe.h>
#include <glib.h>

#include <commondb/dudldb.h>

typedef void *(*db_convert)( PGresult *res, int tup );

typedef struct {
	PGresult *res;
	db_convert conv;
	int tuple;
} _it_db;

const char *db_errstr( void );

PGresult *db_query( char *query, ... );
_it_db *db_iterate( db_convert func, char *query, ... );

int db_table_exists( char *table );

char *db_escape( const char *in );

int pgint( PGresult *res, int tup, int field );
unsigned int pguint( PGresult *res, int tup, int field );
gint64 pgint64( PGresult *res, int tup, int field );
guint64 pguint64( PGresult *res, int tup, int field );
double pgdouble( PGresult *res, int tup, int field );
int pgbool( PGresult *res, int tup, int field );
char *pgstring( PGresult *res, int tup, int field );

#endif
