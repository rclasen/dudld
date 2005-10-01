
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>

#include "parseexpr.h"

#ifdef DEBUG
#define DTOKEN(x)	syslog(LOG_DEBUG,"found token: %s\n",x )
#else
#define DTOKEN(x)	{}
#endif

typedef struct {
	parser_input *in;
	char *msg;
} parse_stat;

/************************************************************
 * low-level token reader
 */

static inline void parse_error( parse_stat *i, char *msg )
{
	if( ! i->msg ) i->msg = msg;
}

/* find next non-whitespace char */
static int parse_nonspace( parse_stat *i )
{
	int c;

	do {
		PI_DONE(i->in);
		c = PI_NEXT(i->in);
	} while( isspace(c));

	return c;
}

static char *_parse_alloc( 
		parse_stat *i, 
		int (*filter)( parse_stat * ))
{
	int c;
	char *str;
	int avail = 5;
	int used = 0;

	if( NULL == (str = malloc(5))){ 
		parse_error(i, strerror(errno));
		return NULL;
	}
	*str = 0;
	
	while( EOF != (c = (*filter)(i) )){
		if( used >= avail ){
			char *new;
			avail += 100;
			if( NULL == (new = realloc(str, avail +1) )){
				free(str);
				parse_error(i, strerror(errno));
				return NULL;
			}
			str = new;
		}

		str[used++] = c;
	}
	if( str )
		str[used] = 0;
	return str;
}

static int _filter_name( parse_stat *i )
{
	int c = PI_NEXT(i->in);

	if( c == EOF ){
		PI_DONE(i->in);
		return EOF;
	}

	if( isalnum( c ) ) {
		PI_DONE(i->in);
		return c;
	}
	PI_UNDO(i->in);
	return EOF;
}

static char *parse_name( parse_stat *i )
{
	parse_nonspace(i);
	PI_UNDO(i->in);

	return _parse_alloc( i, _filter_name );
}

static int parse_num( parse_stat *i )
{
	int c;
	int num = 0;

	while( isdigit( c = parse_nonspace(i))){
		PI_DONE(i->in);
		num *= 10;
		num += c - '0'; 
	}
	PI_UNDO(i->in);

	return num;
}

static int _filter_string( parse_stat *i )
{
	int c = PI_NEXT(i->in);
	int e;

	switch(c){
	  case '\\':
		PI_DONE(i->in);

		e = PI_NEXT(i->in);
		switch(e){
		  case '\\':
		  case '"':
			PI_DONE(i->in);
			return e;

		  case EOF:
			parse_error(i, "EOF while expecting escaped char");
			PI_DONE(i->in);
			return c;

		}
		PI_UNDO(i->in);
		return c;

	  case '"':
		PI_UNDO(i->in);
		return EOF;

	  default:
		PI_DONE(i->in);

	}

	return c;
}

static char *parse_string( parse_stat *i )
{
	int c = parse_nonspace(i);
	char *str;

	if( c == EOF ){
		PI_DONE(i->in);
		return NULL;
	}

	if( c != '"' ){
		PI_UNDO(i->in);
		return NULL;
	}

	PI_DONE(i->in);
	if( NULL == (str = _parse_alloc( i, _filter_string )))
		return NULL;

	if( '"' != (c = PI_NEXT(i->in))){
		parse_error(i, "missing string termination" );
		free(str);
		return NULL;
	}

	return str;
}

/************************************************************
 * value parsing
 */

static void value_free( value *v )
{
	value **p;

	switch(v->type){
	  case vt_num:
		  break;

	  case vt_string:
		  free(v->val.string);
		  break;

	  case vt_list:
		  if( ! v->val.list ) break;
		  for( p = v->val.list; *p; ++p )
			  free(*p);
		  free(v->val.list);
		  break;
	}
	free(v);
}

static int value_fmt( char *buf, size_t len, value *v )
{
	size_t used = 0;
	value **p;

	switch(v->type){
	  case vt_num:
		  used += snprintf( buf+used, len-used, "%d",v->val.num );
		  break;

	  case vt_string:
		  used += snprintf( buf+used, len-used, "\"%s\"",
				  v->val.string );
		  break;

	  case vt_list:
		  for( p = v->val.list; *p; ++p ){
			  if( p != v->val.list ){
				  used += snprintf( buf+used, len-used, ", ");
				  if( used > len ) return used;
			  }

			  used += value_fmt( buf+used, len-used, *p );
			  if( used > len ) return used;
		  }
		  break;
	}
	return used;
}

/* parse either string or integer */
static value *parse_value( parse_stat *i )
{
	value *v;
	int c;

	c = parse_nonspace(i);
	PI_UNDO(i->in);

	if( c == EOF ){
		parse_error(i, "expecting a value" );
		goto clean1;
	}

	if( NULL == (v = malloc(sizeof(value)))){
		parse_error(i, strerror(errno));
		goto clean1;
	}

	if( c == '"' ){
		v->type = vt_string;
		if( NULL == (v->val.string = parse_string(i)))
			goto clean2;

	} else if( isdigit(c) ){
		v->type = vt_num;
		v->val.num = parse_num(i);

	} else {
		parse_error( i, "invalid value" );
		goto clean2;
	}

	return v;

clean2:
	free(v);
clean1:
	return NULL;
}

static value *parse_vallist( parse_stat *i )
{
	value *l;
	int used = 0;
	int avail = 0;

	if( NULL == (l = malloc(sizeof(value)))){
		parse_error(i, strerror(errno));
		goto clean1;
	}
	l->type = vt_list;
	l->val.list = NULL;

	do {
		if( used >= avail ){
			value **new;

			avail+=5;
			if( NULL == (new = realloc(l->val.list, (avail+1) *
							sizeof(value*)))){
				parse_error(i, strerror(errno));
				goto clean2;
			}
			l->val.list = new;
		}

		if( NULL == (l->val.list[used++] = parse_value(i))){
			parse_error(i, "missing value" );
			goto clean2;
		}
		l->val.list[used] = NULL;

	} while( ',' == parse_nonspace(i));
	PI_UNDO(i->in);

	return l;
clean2:
	value_free(l);
clean1:
	return NULL;
}


/************************************************************
 * valtest parsing
 */

static valop parse_valop( parse_stat *i )
{
	int c = parse_nonspace(i);
	int n;

	switch(c){

	  case '=':
		  PI_DONE(i->in);
		  return vo_eq;

	  case '>':
		  PI_DONE(i->in);

		  n = PI_NEXT(i->in);
		  if( n == '=' ){
			  PI_DONE(i->in);
			  return vo_ge;
		  }
		  PI_UNDO(i->in);

		  return vo_gt;

	  case '<':
		  PI_DONE(i->in);

		  n = PI_NEXT(i->in);
		  if( n == '=' ){
			  PI_DONE(i->in);
			  return vo_le;
		  }
		  PI_UNDO(i->in);

		  return vo_lt;

	  case '~':
		  PI_DONE(i->in);
		  return vo_re;

	  case 'i':
	  case 'I':
		  n = PI_NEXT(i->in);
		  if( n == 'n' || n == 'N' ){
			  PI_DONE(i->in);
			  return vo_in;
		  }

	  case EOF:
		  PI_DONE(i->in);
		  parse_error( i, "EOF instead of value operator");
		  return vo_none;
	}
	PI_UNDO(i->in);
	return vo_none;
}


static valtest *parse_valtest( parse_stat *i )
{
	valtest *vt;
	char *name;
	int numeric = 0;

	if( NULL == (vt = malloc(sizeof(valtest)))){
		parse_error(i, strerror(errno));
		goto clean1;
	}

	/* get the name */
	if( NULL == (name = parse_name(i))){
		parse_error(i, "expecting a field name" );
		goto clean2;
	}

	// TODO: use valfield_name array
	if( 0 == strcmp( name, "duration" )){
		vt->field = vf_dur;
		numeric++;
	} else if( 0 == strcmp( name, "lastplay" )){
		vt->field = vf_lplay;
		numeric++;
	} else if( 0 == strcmp( name, "tag" )){
		vt->field = vf_tag;
	} else if( 0 == strcmp( name, "artist" )){
		vt->field = vf_artist;
	} else if( 0 == strcmp( name, "title" )){
		vt->field = vf_title;
	} else if( 0 == strcmp( name, "album" )){
		vt->field = vf_album;
	} else if( 0 == strcmp( name, "year" )){
		vt->field = vf_year;
		numeric++;
	} else {
		parse_error( i, "unknown field" );
		free(name);
		goto clean2;
	}
	free(name);


	/* get operator */
	vt->op = parse_valop(i);
	switch(vt->op){
	  case vo_none:
	  case vo_max:
		parse_error( i, "invalid value operator");
	  	goto clean2;

	  case vo_lt:
	  case vo_le:
	  case vo_gt:
	  case vo_ge:
		/*
		if( ! numeric ){
			parse_error( i, "invalid operation for string value" );
			goto clean2;
		}
		*/
		vt->val = parse_value(i);
		break;

	  case vo_re:
		if( numeric ){
			parse_error( i, "invalid operation for numeric value");
			goto clean2;
		}
		vt->val = parse_value(i);
		break;

	  case vo_eq:
		vt->val = parse_value(i);
		break;

	  case vo_in:
		vt->val = parse_vallist(i);
		break;
	}

	if( vt->val == NULL ){
		parse_error(i, "no value specified" );
		goto clean2;
	}

	if( vt->op == vo_re && vt->val->type != vt_string ){
		parse_error(i, "regexp match needs string value" );
		goto clean3;
	}
	
	if( numeric ){
		if( vt->val->type != vt_num ){
			parse_error(i, "need a numeric value" );
			goto clean3;
		}

	} else {
		if( vt->field != vf_tag && vt->val->type != vt_string ){
			// TODO: id lookup
			// TODO: lookup reject should happen from DB module
			parse_error(i, "id lookup not yet allowed");
			goto clean3;
		}
	}


	return vt;

clean3:
	value_free(vt->val);
clean2:
	free(vt);
clean1:
	return NULL;
}

static void valtest_free( valtest *vt )
{
	value_free(vt->val);
	free(vt);
}

static char *valfield_name[vf_max] = {
	"duration",
	"lastplay",
	"tag",
	"artist",
	"title",
	"album",
	"year"
};

static char *valop_name[vo_max] = {
	"",
	"=",
	"<",
	"<=",
	">",
	">=",
	"IN",
	"~",
};

static int valtest_fmt( char *buf, size_t len, valtest *vt )
{
	size_t used = 0;
	
	used += snprintf( buf+used, len-used, "%s %s ", 
			valfield_name[vt->field], valop_name[vt->op] );
	if( used > len ) return used;
	used += value_fmt( buf+used, len-used, vt->val );
	return used;
}
	


/************************************************************
 * expression parsing
 */

static expr *parse_expr( parse_stat *i );
static expr *parse_one_expr( parse_stat *i );

static expr *parse_expr_val( parse_stat *i )
{
	expr *e;

	if( NULL == (e = malloc(sizeof(expr)))){
		parse_error(i,strerror(errno));
		goto clean1;
	}
	e->op = op_self;
	e->_refs = 1;

	if( NULL == (e->data.val = parse_valtest(i)))
		goto clean2;

	return e;

clean2:
	free(e);
clean1:
	return NULL;
}

static expr *parse_expr_not( parse_stat *i)
{
	expr *e;

	if( NULL == (e = malloc(sizeof(expr)))){
		parse_error(i,strerror(errno));
		goto clean1;
	}
	e->op = op_not;
	e->_refs = 1;

	if( NULL == (*e->data.expr = parse_one_expr(i))){
		parse_error(i, "expecting an expression" );
		goto clean2;
	}
	return e;

clean2:
	free(e);
clean1:
	return NULL;
}


static expr *parse_one_expr( parse_stat *i )
{
	int c = parse_nonspace(i);

	if( c == '(' ){
		expr *e;

		DTOKEN( "( - open brace" );
		PI_DONE(i->in);
		if( NULL == (e = parse_expr(i))){
			parse_error(i, "expecting an expression" );
			return NULL;
		}

		if( ')' != parse_nonspace(i) ){
			parse_error(i, "missing close brace" );
			expr_free(e);
			return NULL;
		}
		DTOKEN( ") - matching close brace" );

		PI_DONE(i->in);
		return e;

	} else if( c == ')' ){
		parse_error( i, "unexpected close brace" );
		PI_DONE(i->in);
		return NULL;

	} else if( c == '!' ){
		DTOKEN( "! - not" );
		PI_DONE(i->in);
		return parse_expr_not(i);
	}


	DTOKEN( "valtest" );
	PI_UNDO(i->in);
	return parse_expr_val(i);
}

static operator parse_operator( parse_stat *i )
{
	int c = parse_nonspace(i);

	switch(c){
	  case '|':
		  PI_DONE(i->in);
		  return op_or;

	  case '&':
		  PI_DONE(i->in);
		  return op_and;

	  case EOF:
		  PI_DONE(i->in);
		  return op_none;
	}
	PI_UNDO(i->in);
	return op_none;
}


expr *parse_expr( parse_stat *i )
{
	expr *a, *b, *e;
	operator op;

	if( NULL == (a = parse_one_expr(i)))
		goto clean1;

	if( ')' == parse_nonspace(i) ){
		PI_UNDO(i->in);
		return a;
	}
	PI_UNDO(i->in);
	

	if( op_none == (op = parse_operator(i))) {
		if( ! PI_EOF(i->in)){
			parse_error( i, "invalid operator" );
			goto clean2;
		}
		return a;
	}

	DTOKEN( "&| combination" );

	if( NULL == (b = parse_one_expr(i))) {
		parse_error( i, "expecting another expression for &|" );
		goto clean2;
	}

	if( NULL == (e = malloc(sizeof(expr)))){
		parse_error(i,strerror(errno));
		goto clean3;
	}

	e->op = op;
	e->data.expr[0] = a;
	e->data.expr[1] = b;
	e->_refs = 1;

	return e;

clean3:
	expr_free(b);
clean2:
	expr_free(a);
clean1:
	return NULL;
}
	
expr *expr_parse( int *line, int *col, char **msg, parser_input *i )
{
	parse_stat st;
	expr *e;

	st.in = i;
	st.msg = NULL;

	e = parse_expr( &st );

	if( e && EOF != parse_nonspace(&st)){
		expr_free(e);
		e = NULL;
		st.msg = "junk at end";
	}

	if( line ) *line = e ? -1 : PI_LINE(i);
	if( col ) *col = e ? -1 : PI_COL(i);
	if( msg ) *msg = e ? NULL : st.msg;
	return e;
}

expr *expr_parse_str( int *col, char **msg, char *i )
{
	parser_input *pi;

	if( NULL == (pi = pi_str_new(i))){
		if( msg ) *msg = strerror(errno);
		return NULL;
	}

	return expr_parse( NULL, col, msg, pi );
}

int expr_fmt( char *buf, size_t len, expr *e )
{
	size_t used = 0;

	switch( e->op ){
	  case op_self:
		  used += valtest_fmt( buf+used, len-used, e->data.val );
		  break;

	  case op_not:
		  used += snprintf( buf+used, len-used, "!( " );
		  if( used > len ) return used;
		  used += expr_fmt( buf+used, len-used, *e->data.expr );
		  if( used > len ) return used;
		  used += snprintf( buf+used, len-used, " )" );
		  break;

	  case op_and:
		  used += snprintf( buf+used, len-used, "( " );
		  if( used > len ) return used;
		  used += expr_fmt( buf+used, len-used, e->data.expr[0] );
		  if( used > len ) return used;
		  used += snprintf( buf+used, len-used, " )&( " );
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
		  used += snprintf( buf+used, len-used, " )|( " );
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

void expr_free( expr *e )
{
	if( ! e )
		return;

	if( -- e->_refs > 0 )
		return;

	switch( e->op ){
	  case op_self:
		  valtest_free( e->data.val );
		  break;

	  case op_not:
		  expr_free( *e->data.expr );
		  break;

	  case op_and:
	  case op_or:
		  free(e->data.expr[0]);
		  free(e->data.expr[1]);
		  break;

	  case op_none:
		  break;
	}
	free(e);
}

expr *expr_copy( expr *e )
{
	if( ! e )
		return NULL;

	e->_refs++;
	return e;
}

