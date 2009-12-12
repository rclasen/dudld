/*
 * Copyright (c) 2008 Rainer Clasen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */

#ifndef _COMMONDB_RANDOM_H
#define _COMMONDB_RANDOM_H

#include "track.h"
#include "parseexpr.h"

typedef void (*t_random_func)( void );

int random_init( void );

int random_setfilter( expr *filt );
int random_filterstat( void );
expr *random_filter( void );
it_track *random_top( int num );
t_track *random_fetch( void );

// TODO: cache_update is internal:
int random_cache_update( int id, int lplay );

extern t_random_func random_func_filter;

#endif
