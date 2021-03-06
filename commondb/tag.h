/*
 * Copyright (c) 2008 Rainer Clasen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */

#ifndef _COMMONDB_TAG_H
#define _COMMONDB_TAG_H

#include "dudldb.h"

typedef struct {
	int id;
	char *name;
	char *desc;
} t_tag;

typedef void (*t_tag_func)( t_tag *t );
extern t_tag_func tag_func_changed;
extern t_tag_func tag_func_del;

#define it_tag it_db
#define it_tag_begin(x)		((t_tag*)it_db_begin(x))
#define it_tag_cur(x)		((t_tag*)it_db_cur(x))
#define it_tag_next(x)		((t_tag*)it_db_next(x))
#define it_tag_done(x)		it_db_done(x)

void tag_free( t_tag *t );

t_tag *tag_get( int id );
int tag_id( const char *name );

it_tag *tags_list( void );
it_tag *tags_artist( int aid );

int tag_add( const char *name );
int tag_setname( int id, const char *name );
int tag_setdesc( int id, const char *desc );
int tag_del( int id );

it_tag *track_tags( int tid );
int track_tagadd( int tid, int id );
int track_tagdel( int tid, int id );

int track_tagged( int tid, int id );

// TODO: tagstat

#endif
