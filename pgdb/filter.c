/*
 * Copyright (c) 2008 Rainer Clasen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */

#include <stdio.h>
#include <syslog.h>

#include <config.h>
#include "dudldb.h"
#include "filter.h"

// TODO: report errors

static int add_val( value ***vlist, size_t *num, value *add )
{
	value **tmp;

	if( NULL == (tmp = realloc(*vlist, (*num+1) * sizeof(value))))
		return 1;

	*vlist = tmp;
	tmp[(*num)++] = add;

	return 0;
}

#define SQL_LEN 1024
static value **sql_tags2id( value **inval )
{
	value **val;
	value **idval = NULL;
	size_t idnum = 0;
	char sql[SQL_LEN] = "";
	int used = 0;
	char *esc;
	PGresult *res;
	int tup;


	/* add id values to result and build query to lookup strings */
	for( val = inval; *val ; ++val ){
		switch( (*val)->type ){
		  case vt_num:
			  if( add_val( &idval, &idnum, *val ))
				  goto clean1;
			  break;

		  case vt_string:
			  syslog( LOG_DEBUG, "tag2id: %s", (*val)->val.string );
			  if( used ){
				  used += snprintf(sql+used, SQL_LEN-used, ",");
			  	  if( used > SQL_LEN )
					  goto clean1;
			  }

			  esc = db_escape((*val)->val.string);
			  used += snprintf(sql+used, SQL_LEN-used,"'%s'",esc);
			  free(esc);

			  if( used > SQL_LEN )
				  goto clean1;
			  break;

		  case vt_list:
		  case vt_none:
		  case vt_max:
			  // TODO: report error
			  break;
		}
	}

	if( ! used ){
		add_val(&idval, &idnum, NULL );
		return idval;
	}

	res = db_query( "SELECT id FROM mserv_tag WHERE name IN (%s)", sql );
	if (!res || PQresultStatus(res) != PGRES_TUPLES_OK){
		syslog( LOG_ERR, "sql_tags2id: %s", db_errstr() );
		PQclear(res);
		goto clean1;
	}

	for( tup = 0; tup < PQntuples(res); ++tup ){
		value *v;
		v = malloc(sizeof(value));
		v->type = vt_num;
		v->val.num = pgint(res,tup,0);

		if( add_val( &idval, &idnum, v ))
			goto clean2;

	}

	PQclear(res);
	add_val( &idval, &idnum, NULL );
	return idval;

clean2:
	PQclear(res);
clean1:
	vallist_free(idval);
	return NULL;
}

static char *sql_idlist( value **vlist )
{
	value **val;
	size_t sused = 0;
	static char sbuf[SQL_LEN];

	*sbuf = 0;
	for( val = vlist; *val; ++val ){
		if( val != vlist ){
			sused += snprintf( sbuf+sused, SQL_LEN-sused, ",");
			if( sused > SQL_LEN )
				return NULL;
		}

		sused += snprintf( sbuf+sused, SQL_LEN-sused,
				"%d", (*val)->val.num );
		if( sused > SQL_LEN )
			return NULL;

	}

	return sbuf;

}

static int sql_taglist( char *buf, size_t len, value **vlist )
{
	value **idlist;
	size_t used = 0;
	char *lst;

	if( NULL == ( idlist = sql_tags2id( vlist )))
		return 0;

	if( NULL == (lst = sql_idlist(idlist)))
		goto clean1;

	if( *lst == 0 )
		goto clean1;

	used = snprintf( buf, len,
			"EXISTS( SELECT file_id "
			"FROM mserv_filetag ft "
			"WHERE "
				"t.id = ft.file_id AND "
				"ft.tag_id IN (%s))",
				lst );

clean1:
	vallist_free(idlist);
	return used;
}

static char *oper_names[vo_max] = {
	"", // none
	"=",
	"<",
	"<=",
	">",
	">=",
	"IN",
	"~*",
};

static int sql_vt_num( char *buf, size_t len, valtest *vt, char *row )
{
	return snprintf( buf, len, "%s %s %d",
			row, oper_names[vt->op], vt->val->val.num );
}

static int sql_vt_year( char *buf, size_t len, valtest *vt, char *row )
{
	return snprintf( buf, len, "%s %s '%04d'",
				  row, oper_names[vt->op], vt->val->val.num );
}

static int sql_vt_tag( char *buf, size_t len, valtest *vt, char *row )
{
	value *vlist[2] = { vt->val, NULL };

	(void)row;
	return sql_taglist( buf, len, vlist );
}

static int sql_vt_taglist( char *buf, size_t len, valtest *vt, char *row )
{
	(void)row;
	return sql_taglist( buf, len, vt->val->val.list );
}

static int sql_vt_tagre( char *buf, size_t len, valtest *vt, char *row )
{
	char *esc;
	int used;

	(void)row;
	esc = db_escape(vt->val->val.string);
	used = snprintf( buf, len,
			"EXISTS( SELECT file_id "
			"FROM mserv_filetag ft "
				"INNER JOIN mserv_tag tag "
				"ON ft.tag_id = tag.id "
			"WHERE "
				"t.id = ft.file_id AND "
				"tag.name %s '%s')",
			oper_names[vt->op], esc );
	free(esc);
	return used;
}

static int sql_vt_string( char *buf, size_t len, valtest *vt, char *row )
{
	char *esc;
	int used;

	esc = db_escape(vt->val->val.string); // TODO: lower?
	used = snprintf( buf, len, "%s %s '%s'",
			row, oper_names[vt->op], esc );
	free(esc);
	return used;
}

typedef int (*fmt_func)(char *buf, size_t len, valtest *vt, char *row );
typedef struct {
	valfield field;
	valop op;
	valtype type;
	fmt_func func;
	char *row;
} sql_valtestfmt_t;

static sql_valtestfmt_t sql_valtestfmt[] ={
	{ vf_dur, vo_eq, vt_num, sql_vt_num, "dur" },
	{ vf_dur, vo_lt, vt_num, sql_vt_num, "dur" },
	{ vf_dur, vo_le, vt_num, sql_vt_num, "dur" },
	{ vf_dur, vo_gt, vt_num, sql_vt_num, "dur" },
	{ vf_dur, vo_ge, vt_num, sql_vt_num, "dur" },

	{ vf_lplay, vo_eq, vt_num, sql_vt_num, "lplay" },
	{ vf_lplay, vo_lt, vt_num, sql_vt_num, "lplay" },
	{ vf_lplay, vo_le, vt_num, sql_vt_num, "lplay" },
	{ vf_lplay, vo_gt, vt_num, sql_vt_num, "lplay" },
	{ vf_lplay, vo_ge, vt_num, sql_vt_num, "lplay" },

	{ vf_year, vo_eq, vt_num, sql_vt_year, "album_publish_year" },
	{ vf_year, vo_lt, vt_num, sql_vt_year, "album_publish_year" },
	{ vf_year, vo_le, vt_num, sql_vt_year, "album_publish_year" },
	{ vf_year, vo_gt, vt_num, sql_vt_year, "album_publish_year" },
	{ vf_year, vo_ge, vt_num, sql_vt_year, "album_publish_year" },

	{ vf_tag, vo_eq, vt_num, sql_vt_num, NULL},
	{ vf_tag, vo_eq, vt_string, sql_vt_tag, NULL},
	{ vf_tag, vo_re, vt_string, sql_vt_tagre, NULL},
	{ vf_tag, vo_in, vt_list, sql_vt_taglist, NULL},

	{ vf_title, vo_eq, vt_string, sql_vt_string, "lower(title)" },
	{ vf_title, vo_re, vt_string, sql_vt_string, "title" },

	{ vf_artist, vo_eq, vt_string, sql_vt_string, "lower(artist_name)" },
	{ vf_artist, vo_eq, vt_num, sql_vt_num, "artist_id" },
	// TODO: { vf_artist, vo_in, vt_list, sql_vt_idlist, "artist_name" },
	{ vf_artist, vo_re, vt_string, sql_vt_string, "artist_name" },

	{ vf_album, vo_eq, vt_string, sql_vt_string, "lower(album_name)" },
	{ vf_album, vo_eq, vt_num, sql_vt_num, "album_id" },
	// TODO: { vf_album, vo_in, vt_list, sql_vt_idlist, "album_name" },
	{ vf_album, vo_re, vt_string, sql_vt_string, "album_name" },

	{ vf_pos, vo_eq, vt_num, sql_vt_num, "album_pos" },
	{ vf_pos, vo_lt, vt_num, sql_vt_num, "album_pos" },
	{ vf_pos, vo_le, vt_num, sql_vt_num, "album_pos" },
	{ vf_pos, vo_gt, vt_num, sql_vt_num, "album_pos" },
	{ vf_pos, vo_ge, vt_num, sql_vt_num, "album_pos" },

	{ vf_none, vo_none, vt_none, NULL, NULL },
};

static int sql_valtest( char *buf, size_t len, valtest *vt )
{
	sql_valtestfmt_t *fmt;
	int found = 0;

	for( fmt = sql_valtestfmt; fmt->field != vf_none; ++fmt ){
		if( fmt->field == vt->field
				&& fmt->op == vt->op
				&& fmt->type == vt->val->type ){

			found++;
			break;
		}
	}
	if( found ){
		return (*fmt->func)(buf, len, vt, fmt->row );
	}

	// TODO: report error
	return 0;
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
	  case op_max:
		  // TODO: report error
		  break;
	}
	return used;
}




