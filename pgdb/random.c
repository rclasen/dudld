#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <syslog.h>

#include <commondb/random.h>
#include "dudldb.h"
#include "track.h"




expr *filter = NULL;


#define RANDOM_LIMIT 1000

static int sql_value( char *buf, size_t len, value *v )
{
	size_t used = 0;
	char *esc;

	switch(v->type){
	  case vt_num:
		  used += snprintf( buf+used, len-used, "%d",v->val.num );
		  break;

	  case vt_string:
		  esc = db_escape(v->val.string);
		  used += snprintf( buf+used, len-used, "'%s'", esc );
		  free(esc);
		  break;

	  default:
		  break;
	}
	return used;
}

static int sql_taglist( char *buf, size_t len, value **lst )
{
	size_t used = 0;
	value **v;
	char sql[512];
	size_t sus = 0;
	char *esc;
	PGresult *res;
	int tup;

	*sql = 0;
	for( v = lst; *v; ++v ){
		/* add id to buf */
		if( (*v)->type != vt_string ){
			if( used ){
				used += snprintf( buf+used, len-used, 
						", " );
				if( used > len ) return used;
			}

			used += snprintf( buf+used, len-used, "%d", 
					(*v)->val.num );
			if( used > len ) return used;

			continue;
		}

		/*  and build a list of strings for searching their IDs */
		if( v != lst ){
			sus += snprintf( sql+sus, 512-sus, ", " );
			if( sus > 512 ) return 0;
		}

		esc = db_escape((*v)->val.string);
		sus += snprintf( sql+sus, 512-sus, "'%s'", esc );
		free(esc);
		if( sus > 512 ) return 0;
	}

	if( ! sus )
		return used;

	res = db_query( "SELECT id FROM mserv_tag WHERE name IN (%s)", sql );
	if (!res || PQresultStatus(res) != PGRES_TUPLES_OK){
		syslog( LOG_ERR, "sql_taglist: %s", db_errstr() );
		PQclear(res);
		return used;
	}

	for( tup = 0; tup < PQntuples(res); ++tup ){
		if( used ){
			used += snprintf( buf+used, len-used, ", " );
			if( used > len ) goto clean;
		}

		used += snprintf( buf+used, len-used, "%s", 
				PQgetvalue(res,tup,0));
		if( used > len ) goto clean;
	}

clean:
	PQclear(res);
	return used;
}

static int sql_tag( char *buf, size_t len, valtest *vt )
{
	size_t used = 0;

	switch( vt->op ){
	  case vo_eq:
		  used += snprintf( buf+used, len-used, "mserv_tagged(id," );
		  if( used > len ) return used;
		  used += sql_value( buf+used, len-used, vt->val );
		  if( used > len ) return used;
		  used += snprintf( buf+used, len-used, ")" );
		  break;

	  case vo_in:
		  used += snprintf( buf+used, len-used, 
				  "EXISTS( SELECT file_id "
				  "FROM mserv_filetag ft "
				  "WHERE "
				   	"t.id = ft.file_id AND "
					"ft.tag_id IN ("
					);
		  if( used > len ) return used;
		  used += sql_taglist( buf+used, len-used, vt->val->val.list );
		  if( used > len ) return used;
		 
		  used += snprintf( buf+used, len-used, "))" );
		  break;

	  default:
		break;

	}
	return used;
}

char *field_names[vf_max] = {
	"dur",
	"lplay",
	"",
	"artist_id",
	"title",
	"album_id",
};

char *oper_names[vo_max] = {
	"",
	"=",
	"<",
	"<=",
	">",
	">=",
	"IN",
};

static int sql_valtest( char *buf, size_t len, valtest *vt )
{
	size_t used = 0;


	switch(vt->field){
	  case vf_tag:
		  used += sql_tag( buf+used, len-used, vt );
		  break;

	  case vf_dur:
	  case vf_lplay:
		  used += snprintf( buf+used, len-used, "%s %s ", 
				  field_names[vt->field], 
				  oper_names[vt->op] );
		  if( used > len ) return used;
		  used += sql_value( buf+used, len-used, vt->val );
		  break;

	  case vf_title:
	  case vf_artist:
	  case vf_album:
	  case vf_max:
		  // TODO
		  return 0;
	}

	return used;
}

static int sql_expr( char *buf, size_t len, expr *e )
{
	size_t used = 0;

	switch( e->op ){
	  case op_self:
		  used += sql_valtest( buf+used, len-used, e->data.val );
		  break;

	  case op_not:
		  used += snprintf( buf+used, len-used, "NOT ( " );
		  if( used > len ) return used;
		  used += sql_expr( buf+used, len-used, *e->data.expr );
		  if( used > len ) return used;
		  used += snprintf( buf+used, len-used, " )" );
		  break;

	  case op_and:
		  used += snprintf( buf+used, len-used, "( " );
		  if( used > len ) return used;
		  used += expr_fmt( buf+used, len-used, e->data.expr[0] );
		  if( used > len ) return used;
		  used += snprintf( buf+used, len-used, " )AND( " );
		  if( used > len ) return used;
		  used += expr_fmt( buf+used, len-used, e->data.expr[1] );
		  if( used > len ) return used;
		  used += snprintf( buf+used, len-used, " )" );
		  break;

	  case op_or:
		  used += snprintf( buf+used, len-used, "( " );
		  if( used > len ) return used;
		  used += expr_fmt( buf+used, len-used, e->data.expr[0] );
		  if( used > len ) return used;
		  used += snprintf( buf+used, len-used, " )OR( " );
		  if( used > len ) return used;
		  used += expr_fmt( buf+used, len-used, e->data.expr[1] );
		  if( used > len ) return used;
		  used += snprintf( buf+used, len-used, " )" );
		  break;

	  case op_none:
		  break;
	}
	return used;
}



t_random_func random_func_filter = NULL;

int random_setfilter( expr *filt )
{
	PGresult *res;
	char where[4096];

	/* flush old filter */
	res = db_query( "DROP TABLE mserv_cache" );
	PQclear(res);
	if( filter ){
		expr_free( filter );
		filter = NULL;
	}

	/* recreate empty cache table - now the filter may fail
	 * and further queries are still vaild */
	res = db_query( "CREATE TEMP TABLE mserv_cache ("
				"id INTEGER,"
				"lplay INTEGER,"
				"filename VARCHAR"
			")");
	if (!res || PQresultStatus(res) != PGRES_COMMAND_OK){
		syslog( LOG_ERR, "setfilter: %s", db_errstr() );
		PQclear(res);
		return 1;
	}
	PQclear(res);

	*where = 0;
	if( filt )
		sql_expr(where, 4096, filt);

	/* fill cache - if possible */
	res = db_query( "INSERT INTO mserv_cache "
			"SELECT id, lplay, filename "
			"FROM mserv_track t "
			"%s%s",
			filt && *where ? "WHERE " : "",
			filt && *where ? where : ""
			);
	if( ! res || PGRES_COMMAND_OK !=  PQresultStatus(res) ){
		syslog( LOG_ERR, "setfilter: %s", db_errstr() );
		PQclear(res);
		return 1;
	}
	PQclear(res);

	/* remember filter string */
	filter = filt;

	if( random_func_filter )
		(*random_func_filter)();

	/* try to create index for cache table */
	res = db_query( "CREATE INDEX mserv_cache_idx "
			"ON mserv_cache(id)" );
	PQclear(res);

	return 0;
}

int random_cache_update( int id, int lplay )
{
	PGresult *res;

	res = db_query( "UPDATE mserv_cache SET lplay = %d WHERE id = %d",
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

	res = db_query( "SELECT count(*) FROM mserv_cache" );
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
			"SELECT "
				"t.id,"
				"t.album_id,"
				"t.album_pos,"
				"time2unix(t.duration) AS dur,"
				"c.lplay,"
				"t.title,"
				"t.artist_id,"
				"c.filename "
			"FROM "
				"( SELECT * "
					"FROM mserv_cache "
					"ORDER by lplay "
					"LIMIT %d "
				") AS c "
					"INNER JOIN stor_file t "
					"ON t.id = c.id "
			"WHERE "
				"t.title NOTNULL ", num );
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
	res = db_query( "SELECT id FROM mserv_cache "
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


