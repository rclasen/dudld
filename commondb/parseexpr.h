#ifndef _PARSEEXPR_H
#define _PARSEEXPR_H

#include <unistd.h>

#include "parsebuf.h"

typedef enum {
	vt_num,
	vt_string,
	vt_list,
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
	vf_dur,
	vf_lplay,
	vf_tag,
	vf_artist,
	vf_title,
	vf_album,
	vf_year,

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
} operator;

typedef struct s_expr {
	operator op;
	union {
		valtest *val;
		struct s_expr *expr[2];
	} data;
} expr;
	
expr *expr_parse( int *line, int *col, char **msg, parser_input *i );
expr *expr_parse_str( int *col, char **msg, char *i );

int expr_fmt( char *buf, size_t len, expr *e );
void expr_free( expr *e );

#endif
