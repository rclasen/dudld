#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "filter_defs.h"

// typedef t_bool_oper;
// typedef t_bool;

typedef enum {
	t_eq,
	t_in,

	t_max
} e_check_type;

static char *check_types[t_max] = {
	"=",
	"IN",
};

typedef enum {
	f_genre,
	f_artist,
	f_album,
	f_title,
	f_year,
	f_duration,
	f_lastplay,

	f_max
} e_check_field;

static char *check_fields[f_max] = {
	"genres",
	"artist",
	"album",
	"title",
	"year",
	"duration",
	"lastplay",
};


typedef enum {
	at_id,
	at_string,
	at_num,
} e_check_argtype;

typedef struct {
	e_check_argtype type;
	union {
		int id;
		char *string;
		double num;
	} val;
} t_check_arg;

typedef struct {
	e_check_field field;
	e_check_type type;
	int argc;
	t_check_arg *argv;
} t_bool_check_data;




void bool_done( t_bool *in )
{
	(*in->type->done)( in->data );
	free(in);
}

/************************************************************
 * make a bool from a check
 */

static int fmt_arg( char *buf, size_t len, t_check_arg *arg )
{
	unsigned int used = 0;

	switch( arg->type ){
	  case at_id:
		  used += snprintf( buf+used, len-used, " %d", 
				  arg->val.id );
		  break;

	  case at_string:
		  used += snprintf( buf+used, len-used, " \"%s\"",
				  arg->val.string );
		  break;

	  case at_num:
		  used += snprintf( buf+used, len-used, " %f",
				  arg->val.num );
		  break;
	}
	return used;
}

static int bool_check_fmt( char *buf, size_t len, void *fdat )
{
	t_bool_check_data *dat = (t_bool_check_data*) fdat;
	unsigned int used=0;

	used += snprintf( buf, len, "%s", check_fields[dat->field] );
	if( used > len )
		return used;

	if( dat->type == t_in ){
		int i;

		used += snprintf( buf+used, len-used, " IN (" );
		if( used > len )
			return used;

		for( i = 0; i < dat->argc; ++i ){
			if( i ){
				used += snprintf( buf+used, len-used, "," );
				if( used > len )
					return used;
			}
			used += fmt_arg( buf+used, len-used, &dat->argv[i] );
			if( used > len )
				return used;
		}

		used += snprintf( buf+used, len-used, " )");
		return used;

	}

	used += snprintf( buf+used, len-used, " %s", 
			check_types[dat->type] );
	if( used > len )
		return used;

	used += fmt_arg( buf+used, len-used, &dat->argv[0] );
	return used;
}

static int bool_check_match( void *mdat, void *fdat )
{
	(void) mdat;
	(void) fdat;
	// TODO
	return 0;
}

static void *bool_check_copy( void *fdat )
{
	(void) fdat;
	// TODO
	return NULL;
}

static void bool_check_done( void *fdat )
{
	t_bool_check_data *dat = (t_bool_check_data*) fdat;
	int i;

	for(i = 0; i < dat->argc; ++i ){
		if(dat->argv[i].type == at_string )
			free(dat->argv[i].val.string);
	}
	free(dat->argv);
	free(dat);
}

t_bool_oper bool_check_op = { 
	.fmt = bool_check_fmt, 
	.match = bool_check_match, 
	.done = bool_check_done,
	.copy = bool_check_copy,
};

// TODO: deal with non-strings
t_bool *bool_check( const char *field, const char *check, ... )
{
	t_bool *nb;
	t_bool_check_data *dat;
	int i;
	va_list ap;
	char *arg;
	int avail = 0;

	if( NULL == (nb = malloc(sizeof(t_bool))))
		goto clean1;
	nb->type = &bool_check_op;

	if( NULL == (dat = malloc(sizeof(t_bool_check_data))))
		goto clean2;
	nb->data = dat;

	dat->type = t_max;
	for(i = 0; i < t_max; ++i ){
		if( 0 == strcmp(check,check_types[i])){
			dat->type = i;
			break;
		}
	}
	if( dat->type == t_max )
		// TODO: set errno
		goto clean3;

	dat->field = f_max;
	for(i = 0; i < f_max; ++i ){
		if( 0 == strcmp(field,check_fields[i])){
			dat->field = i;
			break;
		}
	}
	if( dat->field == f_max )
		// TODO: set errno
		goto clean3;

	// TODO: check field + dat->type

	/* fill argv, argc */
	dat->argv = NULL;
	dat->argc = 0;
	va_start( ap, check );
	while( NULL != (arg = va_arg( ap, char * ))){
		if( dat->argc >= avail ){
			t_check_arg *new;
			avail += 10;
			if( NULL == (new = realloc( dat->argv,
			    sizeof(t_check_arg)*avail ))){
				goto clean3;
			}
			dat->argv = new;
		}

		dat->argv[dat->argc].type = at_string;
		if( NULL == (dat->argv[dat->argc++].val.string = strdup(arg)))
			goto clean3;
	}
	va_end(ap);


	return nb;

clean3:
	if( dat->argv ){
		for( i = 0; i < dat->argc; ++i ){
			if( dat->argv[i].type == at_string )
				free(dat->argv[i].val.string);
		}
		free( dat->argv );
	}
	free(dat);
clean2:
	free(nb);
clean1:
	return NULL;
}
		

/************************************************************
 * 
 */

static int bool_not_fmt( char *buf, size_t len, void *fdat )
{
	t_bool *dat = (t_bool*) fdat;
	unsigned int used=0;

	used += snprintf( buf, len, "! (");
	if( used > len )
		return used;

	used += bool_fmt(buf+used, len-used, dat );
	if( used > len )
		return used;

	used += snprintf( buf+used, len-used, " )");

	return used;
}

static int bool_not_match( void *mdat, void *fdat )
{
	(void) mdat;
	(void) fdat;
	// TODO
	return 0;
}

static void *bool_not_copy( void *fdat )
{
	(void) fdat;
	// TODO
	return NULL;
}

static void bool_not_done( void *fdat )
{
	t_bool *dat = (t_bool*) fdat;
	bool_done(dat);
}

t_bool_oper bool_not_op = { 
	.fmt = bool_not_fmt, 
	.match = bool_not_match, 
	.done = bool_not_done,
	.copy = bool_not_copy,
};

t_bool *bool_not( t_bool *b )
{
	t_bool *nb;

	if( NULL == (nb = malloc(sizeof(t_bool))))
		return NULL;

	nb->type = &bool_not_op;
	// TODO: copy b
	nb->data = b;

	return nb;
}
		
/************************************************************
 * 
 */

static int bool_and_fmt( char *buf, size_t len, void *fdat )
{
	t_bool **dat = (t_bool**) fdat;
	unsigned int used=0;

	used += snprintf( buf, len, "( ");
	if( used > len )
		return used;

	used += bool_fmt(buf+used, len-used, dat[0] );
	if( used > len )
		return used;

	used += snprintf(buf+used, len-used, " & " );
	if( used > len )
		return used;

	used += bool_fmt(buf+used, len-used, dat[1] );
	if( used > len )
		return used;

	used += snprintf( buf+used, len-used, " )");

	return used;
}

static int bool_and_match( void *mdat, void *fdat )
{
	(void) mdat;
	(void) fdat;
	// TODO
	return 0;
}

static void *bool_and_copy( void *fdat )
{
	(void) fdat;
	// TODO
	return NULL;
}

static void bool_and_done( void *fdat )
{
	t_bool **dat = (t_bool**) fdat;

	bool_done(dat[0]);
	bool_done(dat[1]);
	free(dat);
}

t_bool_oper bool_and_op = { 
	.fmt = bool_and_fmt, 
	.match = bool_and_match, 
	.done = bool_and_done,
	.copy = bool_and_copy,
};

t_bool *bool_and( t_bool *b1, t_bool *b2 )
{
	t_bool *nb;
	t_bool **dat;

	if( NULL == (nb = malloc(sizeof(t_bool))))
		goto clean1;
	nb->type = &bool_and_op;

	if( NULL == (dat = malloc(3*sizeof(t_bool*))))
		goto clean2;
	nb->data = dat;
	
	// TODO: copy b
	dat[0] = b1;
	dat[1] = b2;
	dat[2] = NULL;

	return nb;
clean2:
	free(nb);
clean1:
	return NULL;
}
		
/************************************************************
 * 
 */

static int bool_or_fmt( char *buf, size_t len, void *fdat )
{
	t_bool **dat = (t_bool**) fdat;
	unsigned int used=0;

	used += snprintf( buf, len, "( ");
	if( used > len )
		return used;

	used += bool_fmt(buf+used, len-used, dat[0] );
	if( used > len )
		return used;

	used += snprintf(buf+used, len-used, " | " );
	if( used > len )
		return used;

	used += bool_fmt(buf+used, len-used, dat[1] );
	if( used > len )
		return used;

	used += snprintf( buf+used, len-used, " )");

	return used;
}

static int bool_or_match( void *mdat, void *fdat )
{
	(void) mdat;
	(void) fdat;
	// TODO
	return 0;
}

static void *bool_or_copy( void *fdat )
{
	(void) fdat;
	// TODO
	return NULL;
}

static void bool_or_done( void *fdat )
{
	t_bool **dat = (t_bool**) fdat;

	bool_done(dat[0]);
	bool_done(dat[1]);
	free(dat);
}

t_bool_oper bool_or_op = { 
	.fmt = bool_or_fmt, 
	.match = bool_or_match, 
	.done = bool_or_done,
	.copy = bool_or_copy,
};

t_bool *bool_or( t_bool *b1, t_bool *b2 )
{
	t_bool *nb;
	t_bool **dat;

	if( NULL == (nb = malloc(sizeof(t_bool))))
		goto clean1;
	nb->type = &bool_or_op;

	if( NULL == (dat = malloc(3*sizeof(t_bool*))))
		goto clean2;
	nb->data = dat;

	// TODO: copy b
	dat[0] = b1;
	dat[1] = b2;
	dat[2] = NULL;


	return nb;
clean2:
	free(nb);
clean1:
	return NULL;
}

