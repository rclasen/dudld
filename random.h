#ifndef _RANDOM_H
#define _RANDOM_H

#include "track.h"

typedef void (*t_random_func)( void );

int random_setfilter( const char *filt );
int random_filterstat( void );
const char *random_filter( void );
it_track *random_top( int num );
t_track *random_fetch( void );

extern t_random_func random_func_filter;

#endif
