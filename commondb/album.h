/*
 * Copyright (c) 2008 Rainer Clasen
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */

#ifndef _COMMONDB_ALBUM_H
#define _COMMONDB_ALBUM_H

#include "dudldb.h"
#include "artist.h"

typedef struct _t_album {
	int id;
	char *album;
	int year;
	t_artist *artist;
	double rgain;
	double rgainpeak;
} t_album;

#define it_album it_db
#define it_album_begin(x)	((t_album*)it_db_begin(x))
#define it_album_cur(x)		((t_album*)it_db_cur(x))
#define it_album_next(x)	((t_album*)it_db_next(x))
#define it_album_done(x)	it_db_done(x)

void album_free( t_album *t );

t_album *album_get( int id );

it_album *albums_list( void );
it_album *albums_artistid( int artistid );
it_album *albums_search( const char *substr );
it_album *albums_tag( int tagid );

int album_setname( int id, const char *name );
int album_setartist( int id, int artistid );
int album_setyear( int id, int year );

#endif
