/*
 * Copyright (c) 2008 Rainer Clasen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */

#ifndef _COMMONDB_ARTIST_H
#define _COMMONDB_ARTIST_H

#include "dudldb.h"

typedef struct _t_artist {
	int id;
	char *artist;
} t_artist;

#define it_artist it_db
#define it_artist_begin(x)	((t_artist*)it_db_begin(x))
#define it_artist_cur(x)	((t_artist*)it_db_cur(x))
#define it_artist_next(x)	((t_artist*)it_db_next(x))
#define it_artist_done(x)	it_db_done(x)

void artist_free( t_artist *t );

t_artist *artist_get( int id );

it_artist *artists_list( void );
it_artist *artists_tag( int tid );
it_artist *artists_search( const char *substr );

int artist_add( const char *name );
int artist_setname( int artistid, const char *name );
int artist_del( int artistid );

int artist_merge( int fromid, int toid );

#endif
