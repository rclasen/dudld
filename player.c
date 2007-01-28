
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
static int gap_id = 0;
static int elapsed_id = 0;

static t_track *curtrack = NULL;
static int curuid = 0;

GstElement *p_src = NULL;
GstElement *p_out = NULL;
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
t_player_func_update player_func_elapsed = NULL;

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

static void gap_finish( void )
{
	g_source_remove(gap_id);
	gap_id = 0;
}

static gint cb_elapsed_timeout( gpointer data )
{
	(void)data;

	if( ! player_func_elapsed ){
		elapsed_id = 0;
		return FALSE;
	}

	(*player_func_elapsed)();

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
	if( GST_STATE(p_src) == GST_STATE_PLAYING )
		return pl_play;

	else if( gap_id )
		return pl_play;

	else if( GST_STATE(p_src) == GST_STATE_PAUSED )
		return pl_pause;

	return pl_stop;
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
	g_object_set( G_OBJECT( p_src), "location", fname, NULL);
	if( gst_element_set_state( p_pipe, GST_STATE_PLAYING ) 
		!= GST_STATE_SUCCESS ){

		syslog(LOG_ERR, "play_gst: failed to play");
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

	// TODO: is stopping the pipe needed??
	if( gst_element_set_state( p_pipe, GST_STATE_READY ) 
		!= GST_STATE_SUCCESS ){
		
		syslog(LOG_ERR, "play_gst: failed to finish");
		db_finish(0);
		return;
	}

	db_finish(complete);
}

static int bp_resume( void )
{
	syslog(LOG_DEBUG, "bp_resume");

	if( GST_STATE(p_src) != GST_STATE_PAUSED )
		return -1;

	if( gst_element_set_state( p_pipe, GST_STATE_PLAYING ) 
		!= GST_STATE_SUCCESS ){
		
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
	
	if( GST_STATE(p_src) != GST_STATE_PLAYING )
		return -1;


	if( gst_element_set_state( p_pipe, GST_STATE_PAUSED ) 
		!= GST_STATE_SUCCESS ){
		
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

static gint cb_eos_idle( gpointer data )
{
	bp_finish(1);
	if( gap ){
		syslog(LOG_DEBUG, "play_gst: gap started");
		gap_id = g_timeout_add(1000 * gap, cb_gap_timeout, data );
	} else {
		bp_start();
	}

	return FALSE;
}

static void cb_eos( GstElement *play, gpointer data )
{
	(void)play;
	/* forward this event to main-thread */
	g_idle_add(cb_eos_idle,data);
}

static gint cb_error_idle( gpointer data )
{
	(void)data;

	if( curtrack )
		syslog(LOG_ERR, "play_gst: failed track id=%d %d/%d", 
				curtrack->id, curtrack->album->id, 
				curtrack->albumnr);

	bp_finish(0);
	//TODO: stop on read/decode, otherwise pause
	if( player_func_stop )
		(*player_func_stop)();

	return FALSE;
}

static void cb_error( GstElement *play,
	GstElement *src,
	GError     *err,
	gchar      *debug,
	gpointer    data)
{
	(void)play;
	(void)src;
	(void)err;
	(void)debug;
	(void)data;
	syslog( LOG_ERR, "play_gst: %s %d %d %s", GST_ELEMENT_NAME(src), err->domain, err->code, err->message);
	/* forward this event to main-thread */
	g_idle_add(cb_error_idle,err);
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

	if( ! gst_element_query( p_out, GST_QUERY_POSITION, &fmt, &pos))
		return 0;

	return pos / GST_SECOND;
}

t_playerror player_jump( int to_sec )
{
	gint64 nanoseconds = to_sec * GST_SECOND;

	if( pl_stop == bp_status() )
		return PE_NOTHING;

	gst_element_seek( p_out, GST_SEEK_METHOD_SET | GST_FORMAT_TIME |
			GST_SEEK_FLAG_FLUSH, nanoseconds);

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

const void *player_popt_table( void )
{
	return gst_init_get_popt_table( );
}

void player_init( void )
{
	GstElement *p_dec;
	GstElement *p_scale;
	GstElement *p_conv;
	GstCaps *scaps;
	

	if( NULL == (p_src = gst_element_factory_make ("filesrc", "p_src"))){
		syslog(LOG_ERR,"player: cannot create src object");
		exit(1);
	}

	if( NULL == (p_dec = gst_element_factory_make ("mad", "p_dec"))){
		syslog(LOG_ERR,"player: cannot create decode object");
		exit(1);
	}
	gst_element_link( p_src, p_dec );

	if( NULL == (p_scale = gst_element_factory_make ("audioscale", "p_scale"))){
		syslog(LOG_ERR,"player: cannot create scale object");
		exit(1);
	}
	gst_element_link( p_dec, p_scale );

	if( NULL == (p_conv = gst_element_factory_make ("audioconvert", "p_conv"))){
		syslog(LOG_ERR,"player: cannot create convert object");
		exit(1);
	}
	gst_element_link( p_scale, p_conv );

	if( NULL == (scaps = gst_caps_new_simple ("audio/x-raw-int",
		"format", G_TYPE_STRING, "int",
		"endianness", G_TYPE_INT, 1234,
		"signed", G_TYPE_BOOLEAN, "true",
		"rate", G_TYPE_INT, 44100,
		"channels", G_TYPE_INT, 2,
		"width", G_TYPE_INT, 16,
		"depth", G_TYPE_INT, 16,
		NULL))){

		syslog(LOG_ERR,"player: cannot create caps object");
		exit(1);
	}

	if( NULL == (p_out = gst_element_factory_make ("udpsink", "p_out" ))){
		syslog(LOG_ERR,"player: cannot create output object");
		exit(1);
	}
	gst_util_set_object_arg(G_OBJECT(p_out), "host", "239.0.0.1" );
	gst_util_set_object_arg(G_OBJECT(p_out), "port", "4953" );
	gst_util_set_object_arg(G_OBJECT(p_out), "control", "1" );
	gst_element_link_filtered( p_conv, p_out, scaps );

	if( NULL == (p_pipe = gst_thread_new("p_pipe"))){
		syslog(LOG_ERR,"player: cannot create pipe object");
		exit(1);
	}

	g_signal_connect( p_pipe, "eos", G_CALLBACK(cb_eos), NULL);
	g_signal_connect( p_pipe, "error", G_CALLBACK(cb_error), NULL);
	gst_bin_add_many( GST_BIN(p_pipe), p_src, p_dec, p_scale, p_conv, p_out, NULL);

	gst_element_set_state (p_pipe, GST_STATE_READY);
}

void player_done( void )
{
	player_stop();
	gst_element_set_state( p_pipe, GST_STATE_NULL);
	gst_object_unref( GST_OBJECT( p_pipe));
}
