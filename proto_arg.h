/*
 * Copyright (c) 2008 Rainer Clasen
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */

#ifndef _PROTO_ARG_H
#define _PROTO_ARG_H

#include <stdlib.h>
#include "proto_helper.h"
#include "proto_val.h"

/*
 * - default types for casting in cmd_* functions
 * - pre'define'd struct members for argument lists
 */

#define arg_end { NULL, NULL, NULL }

typedef int t_arg_bool;
#define arg_bool { "bool", APARSE(val_int), NULL }

// TODO: dedicated filter parser
typedef char * t_arg_filter;
#define arg_filter { "filter", APARSE(val_string), AFREE(free) }

typedef int t_arg_id;
#define arg_id { "id", APARSE(val_uint), NULL }

typedef char * t_arg_name;
#define arg_name { "name", APARSE(val_name), AFREE(free) }

typedef unsigned int t_arg_num;
#define arg_num { "num", APARSE(val_uint), NULL }

typedef char * t_arg_pass;
#define arg_pass { "pass", APARSE(val_string), AFREE(free) }

typedef t_rights t_arg_right;
#define arg_right { "right", APARSE(val_uint), NULL }

typedef t_replaygain t_arg_replaygain;
#define arg_replaygain { "replaygain", APARSE(val_replaygain), NULL }

typedef int t_arg_sec;
#define arg_sec { "sec", APARSE(val_uint), NULL }

typedef double *t_arg_decibel;
#define arg_decibel { "decibel", APARSE(val_double), AFREE(free) }

typedef char *t_arg_string;
#define arg_string { "string", APARSE(val_string), AFREE(free) }

#endif
