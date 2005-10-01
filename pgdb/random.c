#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <syslog.h>

#include <config.h>
#include <commondb/random.h>
#include "dudldb.h"
#include "track.h"
#include "filter.h"




expr *filter = NULL;


#define RANDOM_LIMIT 1000

t_random_func random_func_filter = NULL;

static int fill_cache( expr *filt )
{
	PGresult *res;
	char where[4096];

	*where = 0;
	if( filt )
		sql_expr(where, 4096, filt);

	/* fill cache - if possible */
	res = db_query( "INSERT INTO juke_cache "
			"SELECT id, lplay, filename "
			"FROM mserv_track t "
			"%s%s",
			filt && *where ? "WHERE " : "",
			filt && *where ? where : ""
			);
	if( ! res || PGRES_COMMAND_OK !=  PQresultStatus(res) ){
		syslog( LOG_ERR, "fill_cache: %s", db_errstr() );
		PQclear(res);
		return -1;
	}

	PQclear(res);
	return 0;
}

static int create_cache( void )
{
	PGresult *res;

	/* recreate empty cache table - now the filter may fail
	 * and further queries are still vaild - but wont pick any results */
	res = db_query( "CREATE TEMP TABLE juke_cache ("
				"id INTEGER,"
				"lplay INTEGER,"
				"filename VARCHAR"
			")");
	if (!res || PQresultStatus(res) != PGRES_COMMAND_OK){
		syslog( LOG_ERR, "create_cache: %s", db_errstr() );
		PQclear(res);
		return 1;
	}
	PQclear(res);
	return 0;
}

int random_init( void )
{
	return create_cache();
}

int random_setfilter( expr *filt )
{
	PGresult *res;

	/* flush old filter */
	res = db_query( "DROP TABLE juke_cache" );
	PQclear(res);
	expr_free( filter );
	filter = NULL;

	if( create_cache() )
		return 1;

	/* try filling cache - retry with reset filter */
	if( fill_cache( filt ) ){
		if( ! filt )
			goto clean1;

		filter = NULL;
		if( fill_cache(NULL) )
			goto clean1;
	} else {
		filter = expr_copy(filt);
	}

	if( random_func_filter )
		(*random_func_filter)();

	/* try to create index for cache table */
	res = db_query( "CREATE INDEX juke_cache_idx "
			"ON juke_cache(id)" );
	PQclear(res);

	return 0;

clean1:
	syslog( LOG_ERR, "setfilter: cannot fill cache table" );
	return 1;
}

int random_cache_update( int id, int lplay )
{
	PGresult *res;

	res = db_query( "UPDATE juke_cache SET lplay = %d WHERE id = %d",
			lplay, id );
	if( ! res || PQresultStatus(res) != PGRES_COMMAND_OK ){
		syslog( LOG_ERR, "random_cache_update: %s", db_errstr());
		PQclear(res);
		return -1;
	}

	PQclear(res);
	return 0;
}


int random_filterstat( void )
{
	PGresult *res;
	int num;

	res = db_query( "SELECT count(*) FROM juke_cache" );
	if( ! res || PGRES_TUPLES_OK !=  PQresultStatus(res) ){
		syslog( LOG_ERR, "filterstat: %s", db_errstr() );
		PQclear(res);
		return -1;
	}

	num = pgint(res, 0, 0 );
	PQclear(res);

	return num;
}

expr *random_filter( void )
{
	return filter;
}

it_track *random_top( int num )
{
	return db_iterate( (db_convert)track_convert, 
			"SELECT t.*, "
				"c.lplay "
			"FROM "
				"( SELECT * "
					"FROM juke_cache "
					"ORDER by lplay "
					"LIMIT %d "
				") AS c "
					"INNER JOIN mserv_track t "
					"ON t.id = c.id ",
				num );
}

t_track *random_fetch( void )
{
	PGresult *res;
	int num;
	int id;

	num = random_filterstat() / 3;
	if( num > RANDOM_LIMIT )
		num = RANDOM_LIMIT;

	if( num < 1 )
		num = 1;

	/* get first tracks matching filter */
	res = db_query( "SELECT id FROM juke_cache "
			"ORDER BY lplay LIMIT %d", num );
	if( ! res || PGRES_TUPLES_OK !=  PQresultStatus(res) ){
		syslog( LOG_ERR, "random_fetch: %s", db_errstr());
		PQclear(res);
		return NULL;
	}

	/* 
	 * randomly pick a track
	 *
	 * by adding two randoms, we get a Gaussian distribution with
	 * of random numbers between 0 and 2*RAND_MAX with it's max
	 * at RAND_MAX.
	 *
	 * We move this max to zero and take the abs() of the value.
	 *
	 * This way the likeliness of more recent tracks decreases
	 */
	/* num = abs( random() + random() - RAND_MAX ); */
	/*Unum = (double)num / RAND_MAX * PQntuples(res); */

	/* even better: multiply two normalized randoms*/
	num = ((double)random() / RAND_MAX * PQntuples(res)) *
		((double)random() / RAND_MAX * PQntuples(res)) /
		 PQntuples(res);

	/* adjust number to available tracks */
	syslog( LOG_DEBUG, "random: picking %d from top %d", num, 
			PQntuples(res));

	id = pgint( res, num, 0 );
	PQclear(res);


	return track_get( id );
}


