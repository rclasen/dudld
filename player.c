/*
 * Copyright (c) 2008 Rainer Clasen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */


#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/param.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <syslog.h>
#include <errno.h>
#include <assert.h>
#include <math.h>
#include <string.h>
#include <gst/gst.h>

#include <config.h>
#include "opt.h"
#include "commondb/track.h"
#include "commondb/random.h"
#include "commondb/queue.h"
#include "commondb/history.h"
#include "commondb/tag.h"
#include "player.h"


static int do_random = 1;
static int gap = 0;
static int cut = 0;
static t_replaygain rgtype = rg_none;
static double rgpreamp = 0;
static int gap_id = 0;
static int elapsed_id = 0;

static t_track *curtrack = NULL;
static int curuid = 0;

GstElement *p_src = NULL;
GstElement *p_vol = NULL;
GstElement *p_pipe = NULL;

/* used by player_start: */
t_player_func_update player_func_resume = NULL;
/* used by starttrack: */
t_player_func_update player_func_newtrack = NULL;
/* used by update_status: */
t_player_func_update player_func_pause = NULL;
t_player_func_update player_func_stop = NULL;
/* used by player_random */
t_player_func_update player_func_random = NULL;
t_player_func_elapsed player_func_elapsed = NULL;

/************************************************************
 * database functions
 */

/*
 * get next track to play from database
 */
static t_track *db_getnext( void )
{
	t_queue *q;

	if( curtrack ){
		syslog(LOG_NOTICE, "old track still busy");
		return NULL;
	}

	/* queue */
	while( NULL != (q = queue_fetch())){
		curtrack = queue_track(q);
		curuid = q->user->id;
		queue_free(q);

		if( track_exists(curtrack) )
			return curtrack;

		track_free(curtrack);
	}

	curuid = 0;
	if( ! do_random )
		return NULL;

	/* random */
	while( NULL != (curtrack = random_fetch())){
		if( track_exists(curtrack) )
			return curtrack;

		syslog( LOG_INFO, "skipping nonexisting track: %d",
				curtrack->id);
		track_free(curtrack);
	}

	return NULL;
}

static void db_finish( int completed )
{
	if( ! curtrack )
		return;

	// TODO: set "complete" only when track was played at least 50%?
	history_add( curtrack, curuid, completed );
	if( ! completed ){
		int tagid;

		if( 0 < (tagid = tag_id(opt_failtag)))
			track_tagadd(curtrack->id,tagid);
	}


	track_free( curtrack );
	curtrack = NULL;
	curuid = 0;
}

/************************************************************
 * gst backend functions
 */

static int bp_start(void);
static void bp_finish( int complete );
static int bp_resume( void );
static int bp_pause( void );
static t_playstatus bp_status( void );

static void gap_finish( void )
{
	g_source_remove(gap_id);
	gap_id = 0;
}

static gint cb_elapsed_timeout( gpointer data )
{
	gint64 pos;
	GstFormat fmt = GST_FORMAT_TIME;

	(void)data;

	if( ! player_func_elapsed ){
		elapsed_id = 0;
		return FALSE;
	}

	if( ! gst_element_query_position( p_pipe, &fmt, &pos))
		pos = 0;

	(*player_func_elapsed)( pos );

	return TRUE;
}

static void elapsed_add( void )
{
	if( ! player_func_elapsed )
		return;

	if( elapsed_id )
		return;

	elapsed_id = g_timeout_add(1000, cb_elapsed_timeout, NULL );
}

static void elapsed_del( void )
{
	if( ! elapsed_id )
		return;

	g_source_remove( elapsed_id );
	elapsed_id = 0;
}

static t_playstatus bp_status( void )
{
	if( GST_STATE(p_pipe) == GST_STATE_PLAYING )
		return pl_play;

	else if( gap_id )
		return pl_play;

	else if( GST_STATE(p_pipe) == GST_STATE_PAUSED )
		return pl_pause;

	return pl_stop;
}

static int bp_volume( void )
{
	double volume;

	if( ! curtrack )
		return PE_OK;

	volume = rgtype
		? pow( 10, ( (track_rgval( curtrack, rgtype ) + rgpreamp)/20 ) )
		: 1;
	g_object_set( G_OBJECT(p_vol), "volume", volume, NULL );

	return PE_OK;
}

static int bp_seek( gint64 to )
{
	gboolean ret;


	if( cut && curtrack->seg_to && to < (gint64)curtrack->seg_to ){
		syslog(LOG_DEBUG, "bp_seek (%d) %d -> %d (%d)",
			(int)( curtrack->seg_from / GST_SECOND),
			(int)( to / GST_SECOND),
			(int)( curtrack->seg_to / GST_SECOND),
			curtrack->duration );
		ret = gst_element_seek( p_pipe, 1.0, GST_FORMAT_TIME,
			GST_SEEK_FLAG_FLUSH,
			GST_SEEK_TYPE_SET, to,
			GST_SEEK_TYPE_SET, (gint64)curtrack->seg_to);
	} else {
		syslog(LOG_DEBUG, "bp_seek (%d) %d -> end (%d)",
			(int)( curtrack->seg_from / GST_SECOND),
			(int)( to / GST_SECOND),
			curtrack->duration );
		ret = gst_element_seek( p_pipe, 1.0, GST_FORMAT_TIME,
			GST_SEEK_FLAG_FLUSH,
			GST_SEEK_TYPE_SET, to,
			GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE );
	}

	if( ! ret )
		syslog(LOG_ERR, "play_gst: seek failed" );

	return ret;

}

static int bp_start(void)
{
	char fname[MAXPATHLEN];

	syslog(LOG_DEBUG, "bp_start");
	if( bp_status() != pl_stop ){
		syslog(LOG_NOTICE,"gst is still busy");
		return -1;
	}

	/* get next track */
	db_getnext();
	if( NULL == curtrack ){
		if( player_func_stop )
			(*player_func_stop)();
		return PE_NOTHING;
	}

	track_mkpath(fname, MAXPATHLEN, curtrack);
	syslog(LOG_DEBUG, "play_gst: >%s<", fname);
	g_object_set( G_OBJECT(p_src), "location", fname, NULL);

	bp_volume();

	gst_element_set_state( p_pipe, GST_STATE_PAUSED );
	gst_element_get_state( p_pipe, NULL, NULL, GST_CLOCK_TIME_NONE );

	if( cut ){
		bp_seek( curtrack->seg_from ); /* ignore failure */
	} else {
		bp_seek( 0 );
	}

	if( gst_element_set_state( p_pipe, GST_STATE_PLAYING )
		== GST_STATE_CHANGE_FAILURE ){

		syslog(LOG_ERR, "play_gst: failed to play" );
		db_finish(0);

		if( player_func_stop )
			(*player_func_stop)();

		return PE_FAIL;
	}

	if( player_func_newtrack )
		(*player_func_newtrack)();

	elapsed_add();

	return PE_OK;
}

static void bp_finish( int complete )
{
	syslog(LOG_DEBUG, "bp_finish %d", complete);

	elapsed_del();

	if( gap_id )
		gap_finish();

	// stop the pipe completely
	if( gst_element_set_state( p_pipe, GST_STATE_NULL )
		== GST_STATE_CHANGE_FAILURE ){

		syslog(LOG_ERR, "play_gst: failed to finish");
		db_finish(0);
		return;
	}

	db_finish(complete);
}

static int bp_resume( void )
{
	syslog(LOG_DEBUG, "bp_resume");

	if( GST_STATE(p_pipe) != GST_STATE_PAUSED )
		return -1;

	if( gst_element_set_state( p_pipe, GST_STATE_PLAYING )
		== GST_STATE_CHANGE_FAILURE ){

		syslog(LOG_ERR, "play_gst: failed to resume");
		bp_finish(0);
		if( player_func_stop )
			(*player_func_stop)();
		return -1;
	}

	if( player_func_resume )
		(*player_func_resume)();

	elapsed_add();

	return 0;
}

static int bp_pause( void )
{
	syslog(LOG_DEBUG, "bp_pause");

	if( gap_id ){
		gap_finish();

		if( player_func_stop )
			(*player_func_stop)();

		return 0;
	}

	if( GST_STATE(p_pipe) != GST_STATE_PLAYING )
		return -1;


	if( gst_element_set_state( p_pipe, GST_STATE_PAUSED )
		== GST_STATE_CHANGE_FAILURE ){

		syslog(LOG_ERR, "play_gst: failed to finish");
		bp_finish(0);
		if( player_func_stop )
			(*player_func_stop)();
		return -1;
	}

	if( player_func_pause )
		(*player_func_pause)();

	elapsed_del();

	return 0;
}

static gint cb_gap_timeout( gpointer data )
{
	(void)data;

	gap_id = 0;
	bp_start();
	return FALSE;
}

static gboolean cb_bus( GstBus *bus, GstMessage *msg, gpointer data)
{
	GMainLoop *loop = (GMainLoop *) data;
	(void)bus;

	switch (GST_MESSAGE_TYPE (msg)) {

	  case GST_MESSAGE_EOS: {
		gint64 pos;
		GstFormat fmt = GST_FORMAT_TIME;

		if( ! gst_element_query_position( p_pipe, &fmt, &pos))
			pos = 0;

		if( curtrack && pos + 1000000000 < curtrack->seg_to ){
			syslog( LOG_WARNING, "play_gst %d/%d unexpected end %d/%d",
					curtrack->album->id, curtrack->albumnr,
					(int)( pos / GST_SECOND),
					(int)( curtrack->seg_to / GST_SECOND) );
		}

		bp_finish(1);

		if( gap ){
			syslog(LOG_DEBUG, "play_gst: gap started");
			gap_id = g_timeout_add(1000 * gap, cb_gap_timeout, loop );
		} else {
			bp_start();
		}

		break;
	  }

	  case GST_MESSAGE_WARNING: {
		gchar  *debug;
		GError *err;

		gst_message_parse_error (msg, &err, &debug);
		g_free (debug);

		syslog( LOG_WARNING, "play_gst warning: %s %d %d %s",
			GST_ELEMENT_NAME(msg->src),
			err->domain, err->code, err->message );

		g_error_free (err);
		break;
	  }

	  case GST_MESSAGE_ERROR: {
		gchar  *debug;
		GError *err;

		gst_message_parse_error (msg, &err, &debug);
		g_free (debug);

		syslog( LOG_ERR, "play_gst: %s %d %d %s",
			GST_ELEMENT_NAME(msg->src),
			err->domain, err->code, err->message );

		if( curtrack )
			syslog(LOG_ERR, "play_gst: failed track id=%d %d/%d",
					curtrack->id, curtrack->album->id,
					curtrack->albumnr );

		bp_finish(0);

		//TODO: stop on read/decode, otherwise pause
		if( player_func_stop )
			 (*player_func_stop)();

		g_error_free (err);
		break;
	  }

	  default:
		break;
	}

	return TRUE;
}


/************************************************************
 * interface functions
 */

/*
 * return currently playing track
 */
t_track *player_track( void )
{
	if( ! curtrack )
		return NULL;

	// TODO: player_track doesn't tell, who picked this track. This is
	// hidden, till the track is added to the history
	track_use(curtrack);
	return curtrack;
}

/*
 * return current play status
 */
t_playstatus player_status( void )
{
	return bp_status();
}

int player_gap( void )
{
	return gap;
}

t_playerror player_setgap( int g )
{
	gap = g;
	return PE_OK;
}

int player_cut( void )
{
	return cut;
}

t_playerror player_setcut( int g )
{
	cut = g;
	return PE_OK;
}

double player_rgpreamp( void )
{
	return rgpreamp;
}

t_playerror player_setrgpreamp( double g )
{
	rgpreamp = g;
	return bp_volume() ? PE_OK : PE_FAIL;
}

t_replaygain player_rgtype( void )
{
	return rgtype;
}

t_playerror player_setrgtype( t_replaygain g )
{
	rgtype = g;
	return bp_volume() ? PE_OK : PE_FAIL;
}

int player_random( void )
{
	return do_random;
}

t_playerror player_setrandom( int r )
{
	int old = do_random;

	do_random = r ? 1 : 0;

	if( old != do_random && player_func_random )
		(*player_func_random)();

	return PE_OK;
}

int player_elapsed( void )
{
	gint64 pos;
	GstFormat fmt = GST_FORMAT_TIME;

	if( pl_stop == bp_status() )
		return 0;

	if( ! gst_element_query_position( p_pipe, &fmt, &pos))
		return 0;

	return pos / GST_SECOND;
}

t_playerror player_jump( int to_sec )
{
	if( pl_stop == bp_status() )
		return PE_NOTHING;

	if( ! bp_seek( to_sec * GST_SECOND ) )
		return PE_FAIL;

	return PE_OK;
}


/*
 ************************************************************
 * interface functions that affect mode
 */

/*
 * always ensure
 * - current playing track: db_getnext/db_finish
 * - set/del gap callback
 * - send bcast
 * - set gst status: bp_start/bp_finish/bp_pause
 * - set/del elapsed broadcast timer
 */


t_playerror player_pause( void )
{
	t_playstatus mode = bp_status();
	if( mode == pl_pause ){
		return PE_NOTHING;

	} else if( pl_stop ){
		return PE_FAIL;
	}

	if( -1 == bp_pause() )
		return PE_FAIL;
	return PE_OK;
}

/* unpause or start playing */
t_playerror player_start( void )
{
	t_playstatus mode = bp_status();

	if( mode == pl_play ){
		return PE_NOTHING;

	} else if( mode == pl_pause ){
		if( -1 == bp_resume())
			return PE_FAIL;

	} else if( -1 == bp_start()){
		return PE_FAIL;

	}

	return PE_OK;
}

t_playerror player_next( void )
{
	/* TODO: first get next track, then stop current and start next */
	bp_finish(1);
	if( -1 == bp_start() )
		return PE_FAIL;

	return PE_OK;
}

t_playerror player_prev( void )
{
	/* TODO: player_prev */
	return PE_NOTHING;
}


t_playerror player_stop( void )
{
	t_playstatus mode = bp_status();

	if( mode == pl_stop )
		return PE_NOTHING;

	bp_finish(1);
	if( player_func_stop )
		(*player_func_stop)();

	return PE_OK;
}

/*
 ************************************************************
 * interface house keeping fuctions
 */

GOptionGroup *player_options( void )
{
	return gst_init_get_option_group();
}

void player_init( GMainLoop *loop )
{
	GstBus *bus = NULL;
	GstElement *p_dec = NULL;
	GstElement *p_scale = NULL;
	GstElement *p_conv = NULL;
	GstElement *p_out = NULL;
	GError *err = NULL;

	/* TODO: autoplug input to support non-mp3 */

	if( NULL == (p_src = gst_element_factory_make ("filesrc", "p_src"))){
		syslog(LOG_ERR,"player: cannot create src object");
		exit(1);
	}

	if( NULL == (p_dec = gst_element_factory_make ("mad", "p_dec"))){
		syslog(LOG_ERR,"player: cannot create decode object");
		exit(1);
	}

	if( NULL == (p_scale = gst_element_factory_make ("audioresample", "p_scale"))){
		syslog(LOG_ERR,"player: cannot create scale object");
		exit(1);
	}

	if( NULL == (p_conv = gst_element_factory_make ("audioconvert", "p_conv"))){
		syslog(LOG_ERR,"player: cannot create convert object");
		exit(1);
	}

	if( NULL == (p_vol = gst_element_factory_make ("volume", "p_vol"))){
		syslog(LOG_ERR,"player: cannot create volume object");
		exit(1);
	}

	syslog(LOG_DEBUG,"player: constructing output pipeline: %s",
		opt_pipeline );
	if( NULL == (p_out = gst_parse_bin_from_description(
		opt_pipeline, TRUE, &err ))){

		syslog(LOG_ERR,"player: cannot create output pipeline: %s",
			err->message );
		g_error_free (err);
		exit(1);
	}

	if( NULL == (p_pipe = gst_pipeline_new("p_pipe"))){
		syslog(LOG_ERR,"player: cannot create pipe object");
		exit(1);
	}

	bus = gst_pipeline_get_bus (GST_PIPELINE (p_pipe));
	gst_bus_add_watch (bus, cb_bus, loop);
	gst_object_unref (bus);

	gst_bin_add_many( GST_BIN(p_pipe),
		p_src, p_dec, p_scale, p_conv, p_vol, p_out, NULL);

	if( !gst_element_link_many(
		p_src, p_dec, p_scale, p_conv, p_vol, p_out, NULL) )

		syslog( LOG_ERR, "player: failed to link pipeline 1" );


	if( gst_element_set_state (p_pipe, GST_STATE_READY)
		== GST_STATE_CHANGE_FAILURE )

		syslog( LOG_ERR, "play_gst: failed to init pipeline" );
}

void player_done( void )
{
	player_stop();
	gst_element_set_state( p_pipe, GST_STATE_NULL);
	gst_object_unref( GST_OBJECT( p_pipe));
}
