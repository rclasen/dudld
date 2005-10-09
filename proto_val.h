#ifndef _PROTO_VAL_H
#define _PROTO_VAL_H

#include "proto_helper.h"

int val_int( char *in, char **end );
unsigned int val_uint( char *in, char **end );
char *val_name( char *in, char **end );
char *val_string( char *in, char **end );

#endif
