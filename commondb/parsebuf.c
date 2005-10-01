
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <config.h>
#include "parsebuf.h"

void pi_free( parser_input *i )
{
	(*i->free)(i->data);
	free(i);
}

/************************************************************
 *
 * string buffer
 */

typedef struct {
	const char *str;
	size_t cur;
	size_t done;
} pi_str;

#define PI_STR(x) ((pi_str*)(x))

static int pi_str_eof( void *d )
{
	return PI_STR(d)->str[PI_STR(d)->cur] == 0;
}

static int pi_str_line( void *d )
{
	(void)d;
	return -1;
}

static int pi_str_col( void *d )
{
	return PI_STR(d)->cur;
}

static int pi_str_next( void *d )
{
	if( pi_str_eof(d) )
		return EOF;

	return PI_STR(d)->str[PI_STR(d)->cur++];
}

static void pi_str_done( void *d )
{
	PI_STR(d)->done = PI_STR(d)->cur;
}

static void pi_str_undo( void *d )
{
	PI_STR(d)->cur = PI_STR(d)->done;
}

static void pi_str_free( void *d )
{
	free( d );
}

parser_input *pi_str_new( const char *in )
{
	parser_input *pi;
	pi_str *str;

	if( NULL == (pi = malloc(sizeof(parser_input))))
		goto clean1;

	if( NULL == (str = malloc(sizeof(pi_str))))
		goto clean2;

	str->str = in;
	str->cur = 0;
	str->done = 0;

	pi->next = pi_str_next;
	pi->eof = pi_str_eof;
	pi->line = pi_str_line;
	pi->col = pi_str_col;
	pi->done = pi_str_done;
	pi->undo = pi_str_undo;
	pi->free = pi_str_free;
	pi->data = (void*)str;

	return pi;

clean2:
	free(pi);
clean1:
	return NULL;
}


