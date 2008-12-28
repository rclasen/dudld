/*
 * Copyright (c) 2008 Rainer Clasen
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */

#ifndef _PROTO_HELPER_H
#define _PROTO_HELPER_H

#include "client.h"
#include "player.h"
#include "commondb/album.h"
#include "commondb/artist.h"
#include "commondb/user.h"
#include "commondb/history.h"
#include "commondb/random.h"
#include "commondb/queue.h"
#include "commondb/tag.h"
#include "commondb/sfilter.h"

typedef void *(*t_arg_parse)( char *in, char **end );
typedef void (*t_arg_free)( void *data );

typedef struct _t_cmd_arg {
	char *name;
	t_arg_parse parse;
	t_arg_free free;
} t_cmd_arg;

#define APARSE(func)	(t_arg_parse)func
#define AFREE(func)	(t_arg_free)func

typedef void (*t_cmd_run)( t_client *c, char *code, void **argv );

typedef struct _t_cmd {
	char *name;
	char *code;
	t_rights perm;
	t_protstate context;
	t_cmd_run run;
	t_cmd_arg *args;
} t_cmd;

int proto_rline( t_client *client, const char *code, 
		const char *fmt, ... );
int proto_rlast( t_client *client, const char *code, 
		const char *fmt, ... );

void proto_bcast( t_rights right, const char *code, 
		const char *fmt, ... );

void proto_player_reply( t_client *client, t_playstatus r, char *code, char *reply );

#endif
