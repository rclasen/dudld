#ifndef _PARSEEXPR_H
#define _PARSEEXPR_H

#include <unistd.h>

#include "parsebuf.h"

typedef enum {
	vo_none,
	vo_eq,
	vo_lt,
	vo_le,
	vo_gt,
	vo_ge,
	vo_in,

	vo_max,
} valop;

typedef struct {
	char *name;
	valop op;
	char *val;
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
int expr_fmt( char *buf, size_t len, expr *e );
void expr_free( expr *e );

#endif
