/*
 * Copyright (c) 2008 Rainer Clasen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */

#ifndef _PROTO_VAL_H
#define _PROTO_VAL_H

#include "proto_helper.h"

int val_int( char *in, char **end );
unsigned int val_uint( char *in, char **end );
double *val_double( char *in, char **end );
t_replaygain val_replaygain( char *in, char **end );
char *val_name( char *in, char **end );
char *val_string( char *in, char **end );

#endif
