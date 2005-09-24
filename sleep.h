#ifndef _SLEEP_H
#define _SLEEP_H

#include <time.h>

typedef void (*t_sleep_func_set)( void );

extern t_sleep_func_set sleep_func_set;

time_t sleep_remain( void );
void sleep_in( time_t sek );

#endif
