#ifndef _SLEEP_H
#define _SLEEP_H

#include <time.h>


time_t sleep_get( void );
time_t sleep_remain( void );
void sleep_in( time_t sek );
void sleep_at( time_t when );
void sleep_check( void );

#endif
