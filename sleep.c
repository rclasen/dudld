#include <time.h>

#include "player.h"
#include "sleep.h"

static time_t sleepat = 0;

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
	sleepat = time(NULL) + sek;
}

void sleep_at( time_t when )
{
	sleepat = when;
}

void sleep_check( void )
{
	if( ! sleepat || sleepat > time(NULL))
		return;

	player_pause();
	sleepat = 0;
}

// TODO: sleep action: pause or stop?
