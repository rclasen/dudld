#ifndef _RANDOM_H
#define _RANDOM_H

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
