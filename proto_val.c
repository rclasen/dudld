#define _GNU_SOURCE

#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include "proto_val.h"

int val_int( char *in, char **end )
{
	return strtol(in, end, 10 );
}

unsigned int val_uint( char *in, char **end )
{
	return strtoul(in, end, 10 );
}

char *val_name( char *in, char **end )
{
	char *e = in;

	if( end ) *end = in;

	while(isalnum(*e))
		e++;

	if( e == in )
		return NULL;

	if( end )
		*end = e;

	return strndup(in, e - in);
}

char *val_string( char *in, char **end )
{
	if( end )
		*end = in + strlen(in);

	return strdup(in);
}



