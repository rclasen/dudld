
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>

#include "parseexpr.h"

#define DTOKEN(x)	fprintf(stderr,"found token: %s\n",x )

typedef struct {
	parser_input *in;
	char *msg;
} parse_stat;

/************************************************************
 * low-level token reader
 */

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
	char *str = NULL;
	int avail = 0;
	int used = 0;

	
	while( EOF != (c = (*filter)(i) )){
		if( used >= avail ){
			char *new;
			avail += 100;
			if( NULL == (new = realloc(str, avail +1) )){
				free(str);
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


static int _filter_string( parse_stat *i )
{
	int c = PI_NEXT(i->in);
	int e;

	PI_DONE(i->in);
	// TODO: do not swallow terminating "
	switch(c){
	  case '\\':

		e = PI_NEXT(i->in);
		switch(e){
		  case '\\':
		  case '"':
			PI_DONE(i->in);
			return e;

		  case EOF:
			PI_DONE(i->in);
			return c;

		}
		PI_UNDO(i->in);
		return c;

	  case '"':
		return EOF;

	}

	return c;
}

static char *parse_string( parse_stat *i )
{
	int c = parse_nonspace(i);

	if( c == '"' ){
		PI_DONE(i->in);
		return _parse_alloc( i, _filter_string );
	}

	// TODO: bother on missing terminating "

	if( c == EOF )
		PI_DONE(i->in);
	else
		PI_UNDO(i->in);

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

#if 0
	  case 'i':
	  case 'I':
		  n = PI_NEXT(i);
		  if( n == 'n' || n == 'N' ){
			  PI_DONE(i->in);
			  return vo_in;
		  }
#endif
	}
	PI_UNDO(i->in);
	return vo_none;
}

static valtest *parse_valtest( parse_stat *i )
{
	valtest *vt;
	int numeric = 0;

	if( NULL == (vt = malloc(sizeof(valtest))))
		goto clean1;

	/* get the name */
	if( NULL == (vt->name = parse_name(i)))
		goto clean2;

	if( 0 == strcmp( vt->name, "year" )){
		numeric++;
	} else if( 0 == strcmp( vt->name, "duration" )){
		numeric++;
	} else if( 0 == strcmp( vt->name, "lastplay" )){
		numeric++;
	} else if( 0 == strcmp( vt->name, "genre" )){
	} else if( 0 == strcmp( vt->name, "artist" )){
	} else if( 0 == strcmp( vt->name, "title" )){
	} else if( 0 == strcmp( vt->name, "album" )){
	} else {
		i->msg = "unknown field";
		goto clean3;
	}


	/* get operator */
	vt->op = parse_valop(i);
	switch(vt->op){
	  case vo_none:
	  case vo_max:
		i->msg = PI_EOF(i->in) ? 
			"EOF instead of value operator" :
			"invalid value operator";
	  	goto clean3;

	  case vo_lt:
	  case vo_le:
	  case vo_gt:
	  case vo_ge:
		if( ! numeric ){
			i->msg = "invalid operation for string value";
			goto clean3;
		}

	  case vo_eq:
	  case vo_in:
	}

	/* get value(s) */
#if 0
	if( vt->op == vo_in ){
		vt->val = numeric ? parse_num_list(i) : parse_string_list;
	} else {
		vt->val = numeric ? parse_num(i) : parse_string(i);
	}
#endif
	vt->val = parse_string(i);
	if( vt->val == NULL ){
		goto clean3;
	}

	return vt;

clean3:
	free(vt->name);
clean2:
	free(vt);
clean1:
	return NULL;
}

static void valtest_free( valtest *vt )
{
	free(vt->name);
	free(vt->val);
	free(vt);
}

static char *valop_name[vo_max] = {
	"",
	"=",
	"<",
	"<=",
	">",
	">=",
	"IN",
};

static int valtest_fmt( char *buf, size_t len, valtest *vt )
{
	return snprintf( buf, len, "%s %s \"%s\"", 
			vt->name, valop_name[vt->op], vt->val );
}
	


/************************************************************
 * expression parsing
 */

static expr *parse_expr( parse_stat *i );
static expr *parse_one_expr( parse_stat *i );

static expr *parse_expr_val( parse_stat *i )
{
	expr *e;

	if( NULL == (e = malloc(sizeof(expr))))
		goto clean1;
	e->op = op_self;

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

	if( NULL == (e = malloc(sizeof(expr))))
		goto clean1;
	e->op = op_not;

	if( NULL == (*e->data.expr = parse_one_expr(i)))
		goto clean2;

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
		if( NULL == (e = parse_expr(i)))
			return NULL;

		if( ')' != parse_nonspace(i) ){
			expr_free(e);
			return NULL;
		}
		DTOKEN( ") - matching close brace" );

		PI_DONE(i->in);
		return e;

	} else if( c == ')' ){
		i->msg = "unexpected close brace";
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

	if( op_none == (op = parse_operator(i))) {
		if( ! PI_EOF(i->in)){
			i->msg = "invalid operator";
			goto clean2;
		}
		return a;
	}

	DTOKEN( "&| combination" );

	if( NULL == (b = parse_one_expr(i))) {
		i->msg = "expecting another expression for &|";
		goto clean2;
	}

	if( NULL == (e = malloc(sizeof(expr))))
		goto clean3;

	e->op = op;
	e->data.expr[0] = a;
	e->data.expr[1] = b;

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


