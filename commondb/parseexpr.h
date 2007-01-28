#ifndef _COMMONDB_PARSEEXPR_H
#define _COMMONDB_PARSEEXPR_H

#include <unistd.h>

#include "parsebuf.h"

typedef enum {
	vt_none,

	vt_num,
	vt_string,
	vt_list,

	vt_max,
} valtype;

typedef struct s_value {
	valtype type;
	union {
		int num;
		char *string;
		struct s_value **list;
	} val;
} value;

typedef enum {
	vo_none,

	vo_eq,

	vo_lt,
	vo_le,
	vo_gt,
	vo_ge,

	vo_in,
	vo_re,

	vo_max,
} valop;

typedef enum {
	vf_none,

	vf_dur,
	vf_lplay,
	vf_tag,
	vf_artist,
	vf_title,
	vf_album,
	vf_year,
	vf_pos,

	vf_max,
} valfield;

typedef struct {
	valfield field;
	valop op;
	value *val;
} valtest;

typedef enum {
	op_none,

	op_self,
	op_not,
	op_and,
	op_or,

	op_max,
} operator;

typedef struct s_expr {
	operator op;
	union {
		valtest *val;
		struct s_expr *expr[2];
	} data;
	int _refs;
} expr;
	
expr *expr_parse( int *line, int *col, char **msg, parser_input *i );
expr *expr_parse_str( int *col, char **msg, char *i );

int expr_fmt( char *buf, size_t len, expr *e );
void expr_free( expr *e );
expr *expr_copy( expr *e );

void vallist_free( value **v );


#endif
