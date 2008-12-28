/*
 * Copyright (c) 2008 Rainer Clasen
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */

#include <stdlib.h>

#include "player.h"
#include "sleep.h"
#include "proto_fmt.h"
#include "proto_bcast.h"


void proto_bcast_login( t_client *client )
{
	char *buf;

	if( NULL == (buf = mkclient(client)))
		return;
	proto_bcast( r_user, "630", "%s", buf );
	free(buf);
}

void proto_bcast_logout( t_client *client )
{
	char *buf;

	if( NULL == (buf = mkclient(client)))
		return;
	proto_bcast( r_user, "631", "%s", buf ); 
	free(buf);
}

void proto_bcast_player_newtrack( void )
{
	char *buf;
	t_track *track;

	if( NULL == (track = player_track() ))
		return;

	if( NULL != (buf = mktrack(track))){
		proto_bcast( r_guest, "640", "%s", buf ); 
		free(buf);
	}
	track_free(track);
}

void proto_bcast_player_stop( void )
{
	proto_bcast( r_guest, "641", "stopped" );
}

void proto_bcast_player_pause( void )
{
	proto_bcast( r_guest, "642", "paused" );
}

void proto_bcast_player_resume( void )
{
	proto_bcast( r_guest, "643", "resumed" );
}

void proto_bcast_player_random( void )
{
	proto_bcast( r_guest, "646", "%d", player_random() );
}

void proto_bcast_player_elapsed( guint64 elapsed )
{
	proto_bcast( r_guest, "647", "%d", elapsed / 1000000000 );
}

void proto_bcast_sleep( void )
{
	proto_bcast( r_guest, "651", "%d", sleep_remain());
}

void proto_bcast_filter( void )
{
	char buf[1024];
	expr *e;

	e = random_filter();
	if( e )
		expr_fmt( buf, 1024, e );

	proto_bcast( r_guest, "650", "%s", e ? buf : "" );
}

void proto_bcast_queue_fetch( t_queue *q )
{
	char *buf;

	if( NULL == (buf = mkqueue(q)))
		return;
	proto_bcast( r_guest, "660", "%s", buf ); 
	free(buf);
}

void proto_bcast_queue_add( t_queue *q )
{
	char *buf;

	if( NULL == (buf = mkqueue(q)))
		return;
	proto_bcast( r_guest, "661", "%s", buf ); 
	free(buf);
}

void proto_bcast_queue_del( t_queue *q )
{
	char *buf;

	if( NULL == (buf = mkqueue(q)))
		return;
	proto_bcast( r_guest, "662", "%s", buf ); 
	free(buf);
}

void proto_bcast_queue_clear( void )
{
	proto_bcast( r_guest, "663", "queue cleared" );
}

void proto_bcast_tag_changed( t_tag *t )
{
	char *buf;
	if( NULL == (buf = mktag(t)))
		return;
	proto_bcast( r_guest, "670", "%s", buf ); 
	free(buf);
}

void proto_bcast_tag_del( t_tag *t )
{
	char *buf;
	if( NULL == (buf = mktag(t)))
		return;
	proto_bcast( r_guest, "671", "%s", buf ); 
	free(buf);
}


