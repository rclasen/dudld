#include <time.h>

#include "player.h"
#include "sleep.h"

static time_t sleepat = 0;
t_sleep_func_set sleep_func_set = NULL;

time_t sleep_get( void )
{
	return sleepat;
}

time_t sleep_remain( void )
{
	time_t now;

	if( ! sleepat )
		return 0;

	now = time(NULL);
	if( sleepat <= now )
		return 0;

	return sleepat - now;
}

void sleep_in( time_t sek )
{
	time_t old = sleepat;

	sleepat = time(NULL) + sek;

	if( old != sleepat && sleep_func_set )
		(*sleep_func_set)();
}

void sleep_at( time_t when )
{
	time_t old = sleepat;

	sleepat = when;

	if( old != sleepat && sleep_func_set )
		(*sleep_func_set)();
}

void sleep_check( void )
{
	if( ! sleepat || sleepat > time(NULL))
		return;

	player_pause();
	sleepat = 0;
}

// TODO: sleep action: pause or stop?
