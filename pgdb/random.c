#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <syslog.h>

#include <pgdb/db.h>
#include <pgdb/track.h>
#include <random.h>




// TODO: use something real for filter
static char *filter = NULL;


#define RANDOM_LIMIT 1000
#define CACHE_TAB "mserv_cache"

#define CACHE_CREATE "CREATE TEMP TABLE " CACHE_TAB " ("\
	"id INTEGER,"\
	"duration TIME,"\
	"filename VARCHAR"\
	")"

#define CACHE_QUERY \
	"SELECT "\
		"t.id,"\
		"t.album_id," \
		"t.nr,"\
		"date_part('epoch',c.duration) AS dur,"\
		"date_part('epoch',t.lastplay) AS lplay,"\
		"t.title,"\
		"t.artist_id,"\
		"c.filename "\
	"FROM "\
		"mus_title t "\
			"INNER JOIN mserv_cache c "\
			"USING(id) "\
	"WHERE "\
		"t.available "

#define TRACK_QCACHE \
	"SELECT "\
		"t.id,"\
		"f.duration,"\
		"stor_filename(u.collection,u.colnum,f.dir,f.fname) "\
			"AS filename "\
	"FROM "\
		"( "\
			"mus_title t  "\
				"INNER JOIN stor_file f  "\
				"ON t.id = f.titleid "\
			")  "\
			"INNER JOIN stor_unit u  "\
			"ON f.unitid = u.id " \
	"WHERE "\
		"f.titleid NOTNULL "\
		"AND NOT f.broken "\
		"AND t.available "



t_random_func random_func_filter = NULL;

// TODO: dangerous to use the filter unchecked as query
// TODO: set filter asynchronously???
int random_setfilter( const char *filt )
{
	PGresult *res;
	char *n;

	/* flush cache table */
	if( filter ){
		res = db_query( "DROP TABLE " CACHE_TAB );
		PQclear(res);
	}

	/* recreate empty cache table - now the filter may fail
	 * and further queries are still vaild */
	res = db_query( CACHE_CREATE );
	if (!res || PQresultStatus(res) != PGRES_COMMAND_OK){
		syslog( LOG_ERR, "setfilter: %s", db_errstr() );
		PQclear(res);
		return 1;
	}
	PQclear(res);

	/* remember filter string */
	if( NULL == (n=strdup(filt)))
		return 1;
	free( filter );
	filter = n;

	/* fill cache - if possible */
	res = db_query( "INSERT INTO " CACHE_TAB " "
			TRACK_QCACHE "%s%s%s",
			filt && *filt ? "AND (" : "",
			filt && *filt ? filt : "",
			filt && *filt ? ")" : ""
			);
	if( ! res || PGRES_COMMAND_OK !=  PQresultStatus(res) ){
		syslog( LOG_ERR, "setfilter: %s", db_errstr() );
		PQclear(res);
		return 1;
	}
	PQclear(res);

	if( random_func_filter )
		(*random_func_filter)();

	/* try to create index for cache table */
	res = db_query( "CREATE INDEX mserv_cache_idx "
			"ON mserv_cache(id)" );
	PQclear(res);

	return 0;
}

int random_filterstat( void )
{
	PGresult *res;
	int num;

	res = db_query( "SELECT count(*) FROM " CACHE_TAB );
	if( ! res || PGRES_TUPLES_OK !=  PQresultStatus(res) ){
		syslog( LOG_ERR, "filterstat: %s", db_errstr() );
		PQclear(res);
		return -1;
	}

	num = pgint(res, 0, 0 );
	PQclear(res);

	return num;
}

const char *random_filter( void )
{
	return filter;
}

it_track *random_top( int num )
{
	return db_iterate( (db_convert)track_convert,
			CACHE_QUERY "ORDER BY lastplay LIMIT %d", num );
}

t_track *random_fetch( void )
{
	PGresult *res;
	int num;
	t_track *t;

	/* get first tracks matching filter */
	res = db_query( CACHE_QUERY "ORDER BY lastplay LIMIT %d", 
			RANDOM_LIMIT );
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
	num = abs( random() + random() - RAND_MAX );

	/* adjust number to available tracks */
	num = (double)num / RAND_MAX * PQntuples(res);

	t = track_convert( res, num );
	PQclear(res);

	return t;
}



