#ifndef _FILTER_DEFS_H
#define _FILTER_DEFS_H

#include <unistd.h>

/* 
 * methods to operate on boolean expressions.
 * do not use them directly.
 * use bool_*() functions.
 */
typedef struct {
	/* build a human-readable string */
	int (*fmt)( char *buf, size_t len, void *fdat );

	/* implemented by the database: used by filter_set to generate 
	 * the SQL statement or check manually */
	int (*match)( void *mdat, void *fdata );

	/* copy */
	void *(*copy)( void *fdata );

	/* delete all bool data */
	void (*done)( void *fdata );
} t_bool_oper;


/* 
 * boolean expresssion. type points to a record from bools[]
 */
typedef struct {
	t_bool_oper *type;
	void *data;
} t_bool;

struct parse_buf {
	char *str;
	int pos;
	t_bool *res;
};


/* allocate a new bool expression */
t_bool *bool_check( const char *field, const char *check, ... );
t_bool *bool_and( t_bool *a, t_bool *b );
t_bool *bool_or( t_bool *a, t_bool *b );
t_bool *bool_not( t_bool *a );

/* use a bool expression */
#define bool_copy( in ) \
	(*in->type->copy)( in->data )
#define  bool_fmt( buf, len, in ) \
	(*in->type->fmt)( buf, len, in->data )
void bool_done( t_bool *in );


#endif
