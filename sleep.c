/*
 * Copyright (c) 2008 Rainer Clasen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */

#include <time.h>
#include <glib.h>
#include <syslog.h>

#include <config.h>
#include "player.h"
#include "sleep.h"

t_sleep_func_set sleep_func_set = NULL;
time_t sleep_at = 0;
int sleep_id = 0;

static gint sleep_event( gpointer data )
{
	(void)data;

	syslog(LOG_DEBUG, "sleep event");
	player_pause();

	sleep_id = 0;
	sleep_at = 0;
	return FALSE;
}

time_t sleep_remain( void )
{
	time_t now;

	if( ! sleep_at )
		return 0;

	now = time(NULL);
	if( sleep_at <= now )
		return 0;

	return sleep_at - now;
}

void sleep_in( time_t sek )
{
	time_t old = sleep_at;

	if( sleep_id ){
		syslog(LOG_DEBUG, "sleep remove: %d", sleep_id);
		g_source_remove(sleep_id);
	}
	sleep_id = 0;
	sleep_at = 0;

	if( sek > 0 ){
		sleep_id = g_timeout_add(sek * 1000, sleep_event, NULL );
		sleep_at = time(NULL) + sek;
		syslog(LOG_DEBUG, "sleep set: in %u - at %u", (unsigned int)sek, (unsigned int)sleep_at );
	}

	if( old != sleep_at && sleep_func_set )
		(*sleep_func_set)();
}
