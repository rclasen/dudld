#include <stdio.h>
#include <syslog.h>

#include "dudldb.h"
#include "filter.h"


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
	value *eqlist[2] = { NULL, NULL };
	value **list = NULL;
	size_t used = 0;


	switch( vt->op ){
	  case vo_eq:
		  eqlist[0] = vt->val;
		  list = eqlist;
		  break;

	  case vo_in:
		  list = vt->val->val.list;
		  break;

	  default:
		break;

	}

	if( ! list )
		return 0;

	used += snprintf( buf+used, len-used, 
			"EXISTS( SELECT file_id "
			"FROM mserv_filetag ft "
			"WHERE "
				"t.id = ft.file_id AND "
				"ft.tag_id IN ("
				);
	if( used > len ) return used;

	used += sql_taglist( buf+used, len-used, list );
	if( used > len ) return used;

	used += snprintf( buf+used, len-used, "))" );
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
		  // TODO allow other fields than tag
		  return 0;
	}

	return used;
}

int sql_expr( char *buf, size_t len, expr *e )
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
		  used += sql_expr( buf+used, len-used, e->data.expr[0] );
		  if( used > len ) return used;
		  used += snprintf( buf+used, len-used, " )AND( " );
		  if( used > len ) return used;
		  used += sql_expr( buf+used, len-used, e->data.expr[1] );
		  if( used > len ) return used;
		  used += snprintf( buf+used, len-used, " )" );
		  break;

	  case op_or:
		  used += snprintf( buf+used, len-used, "( " );
		  if( used > len ) return used;
		  used += sql_expr( buf+used, len-used, e->data.expr[0] );
		  if( used > len ) return used;
		  used += snprintf( buf+used, len-used, " )OR( " );
		  if( used > len ) return used;
		  used += sql_expr( buf+used, len-used, e->data.expr[1] );
		  if( used > len ) return used;
		  used += snprintf( buf+used, len-used, " )" );
		  break;

	  case op_none:
		  break;
	}
	return used;
}




