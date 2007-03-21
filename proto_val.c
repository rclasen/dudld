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

double *val_double( char *in, char **end )
{
	double *r;

	if( end )
		*end = in;

	if( NULL != (r = malloc(sizeof(double))))
		*r = strtod(in, end );

	return r;
}

t_replaygain val_replaygain( char *in, char **end )
{
	char *tend = NULL;
	unsigned int r = rg_none;
	
	r = strtoul(in, &tend, 10 );
	if( in != tend ){
		switch(r){
		 case rg_none:
		 case rg_track:
		 case rg_track_peak:
		 case rg_album:
		 case rg_album_peak:
			break;

		 default:
		 	r = rg_none;
			tend = in;
			break;
		}
	}

	if( end )
		*end = tend;

	return r;
}

char *val_string( char *in, char **end )
{
	if( end )
		*end = in + strlen(in);

	return strdup(in);
}


