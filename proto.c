/*
 * this protocol is heavily based on SMTP
 *
 * it expexts a single-line command and prefixes each reply with a 3 digit
 * code. The last line of a reply has a ' ' (space) immediately after the
 * code. Other lines have a '-' (dash) after the code. A multiline reply
 * may be terminated by an empty reply ( "xyz \n" )
 *
 * When a reply takes more than a single line, the
 *
 * reply codes:
 *
 * 2yz: ok
 * 3yz: ok, but need more data
 * 5yz: error
 *
 * 6yz: broadcast message
 *
 * x0z: syntax
 * x1z: information
 * x2z: Connection
 *
 * x3z: users
 * x4z: player
 * x5z: filter
 * x6z: queue
 * x7z: tag
 * x8z:
 * x9z:
 *
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <errno.h>

#include "user.h"
#include "history.h"
#include "random.h"
#include "queue.h"
#include "player.h"
#include "sleep.h"
#include "tag.h"
#include "proto.h"

typedef enum {
	arg_none,
	arg_opt,
	arg_need,
} t_args;

#define STRERR strerror(errno)

#define BUFLENLINE	10240
#define BUFLENWHO	100
#define BUFLENTRACK	1024
#define BUFLENTAG	1024

static int proto_vline( t_client *client, int last, const char *code, 
		const char *fmt, va_list ap )
{
	char buffer[BUFLENLINE];
	int r;

	sprintf( buffer, "%3.3s%c", code, last ? ' ' : '-' );

	r = vsnprintf( buffer + 4, BUFLENLINE - 5, fmt, ap );
	if( r < 0 || r > BUFLENLINE - 5 )
		return -1;

	r+=4;
	buffer[r++] = '\n';
	buffer[r++] = 0;

	client_send( client, buffer );

	return 0;
}

/*
 * send non-last line
 */
static int proto_rline( t_client *client, const char *code, 
		const char *fmt, ... )
{
	va_list ap;
	int r;

	va_start( ap, fmt );
	r = proto_vline( client, 0, code, fmt, ap );
	va_end( ap );

	return r;
}

/*
 * send single line reply or last line of a multi-line reply
 */
static int proto_rlast( t_client *client, const char *code, 
		const char *fmt, ... )
{
	va_list ap;
	int r;

	va_start( ap, fmt );
	r = proto_vline( client, 1, code, fmt, ap );
	va_end( ap );

	return r;
}

/*
 * standard reply when a command was invoked with wrong args 
 */
static void proto_badarg( t_client *client, const char *desc )
{
	proto_rlast( client, "501", desc );
}

/*
 * broadcast a reply to all clients with at least "right" rights.
 */
static void proto_bcast( t_rights right, const char *code, 
		const char *fmt, ... )
{
	t_client *c;
	va_list ap;

	va_start( ap, fmt );
	for( c = clients; c; c = c->next ){
		if( c->close )
			continue;

		if( c->right < right )
			continue;

		proto_vline( c, 1, code, fmt, ap );
	}
	va_end( ap );
}


/************************************************************
 * helper
 */

#define EADDC(len,used,buf,c) if(len > used++ ){ *buf++ = c; }
#define EADD(len,used,buf,c) \
	if( c == '\t' ){\
		EADDC(len, used, buf, '\\' );\
		EADDC(len, used, buf, 't' );\
	} else if( c == '\\' ){\
		EADDC(len, used, buf, '\\' );\
		EADDC(len, used, buf, '\\' );\
	} else {\
		EADDC(len, used, buf, c );\
	}


static int ecpy( char *buffer, int len, const char *in )
{
	int used = 0;

	buffer[--len] = 0;

	for( ; *in; ++in ){
		EADD(len, used, buffer, *in );

	}
	if( len > used )
		*buffer++ = 0;

	return used;
}

static int mkvtab( char *buffer, int len, const char *fmt, va_list ap )
{
	int used = 0;
	int l;

	buffer[--len] = 0;

	for( ; *fmt; ++fmt ){
		if( used )
			EADDC(len,used,buffer,'\t');

		if( *fmt == 's' ){
			l = ecpy( buffer, len -used, va_arg( ap, char *));
			used += l;
			buffer += l;

		} else if( *fmt == 'c' ) {
			char c = va_arg( ap, char );
			EADD(len, used, buffer, c );

		} else if( *fmt == 'd' ) {
			l = snprintf( buffer, len -used, "%d", 
					va_arg(ap, int));
			if( l < 0 )
				l = 0;

			used += l;
			buffer += l;

		} else {
			syslog( LOG_CRIT, "inalid format %c in mkvtab", *fmt );
			EADDC(len,used,buffer,'?');

		}
	}
	if( len > used )
		*buffer++ = 0;

	return used;
}

static int mktab( char *buffer, int len, const char *fmt, ... )
{
	va_list ap;
	int r;

	va_start( ap, fmt );
	r = mkvtab( buffer, len, fmt, ap );
	va_end( ap );

	return r;
}

// TODO: check error handling in mkclient, mktrack, mk* ...

/* make a reply string from client data */
static inline char *mkclient( char *buffer, int len, t_client *c )
{
	if( len < mktab( buffer, len, "dds",
				c->id, c->uid, inet_ntoa(c->sin.sin_addr)))
		return NULL;

	return buffer;
}

/* make a reply string from track data */
static inline char *mktrack( char *buffer, int len, t_track *t )
{
	if( len < mktab( buffer, len, "dddsdd", 
				t->id, 
				t->albumid, 
				t->albumnr, 
				t->title, 
				t->artistid, 
				t->duration))
		return NULL;

	return buffer;
}

static inline char *mkhistory( char *buf, int len, t_history *h )
{
	t_track *t;

	t = history_track( h );
	if( len < mktab( buf, len, "dddddsdd",
				h->uid,
				h->played,
				t->id,
				t->albumid, 
				t->albumnr, 
				t->title, 
				t->artistid, 
				t->duration
				)){
		track_free(t);
		return NULL;
	}

	track_free(t);
	return buf;
}

static inline char *mkqueue( char *buf, int len, t_queue *q )
{
	t_track *t;

	t = queue_track(q);
	if( len < mktab(buf, len, "ddddddsdd",
				q->id,
				q->uid,
				q->queued,
				t->id,
				t->albumid, 
				t->albumnr, 
				t->title, 
				t->artistid, 
				t->duration)){
		track_free(t);
		return NULL;
	}

	track_free(t);
	return buf;
}

static inline char *mktag( char *buf, int len, t_tag *t )
{
	if( len < mktab(buf, len, "dss", t->id, t->name, t->desc ))
		return NULL;

	return buf;
}


// TODO: this file is too large. Split out cmds

#define CMD(name, right, state, args )	\
	static void name( t_client *client, char *line )

#define RLINE(code,text...)	proto_rline( client, code,text )
#define RLAST(code,text...)	proto_rlast( client, code,text )
#define RBADARG(desc)		proto_badarg( client, desc )

/************************************************************
 * commands: connection
 */

CMD(cmd_quit, r_any, p_any, arg_none )
{
	(void)line;
	RLAST( "221", "bye" );
	client_close(client);
}

CMD(cmd_disconnect, r_master, p_idle, arg_need )
{
	int id;
	char *end;
	t_client *c;

	id = strtol( line, &end, 10 );
	if( *end ){
		RBADARG( "invalid session ID" );
		return;
	}

	for( c = clients; c; c = c->next ){
		if( c->id == id ){
			proto_rlast( c, "632", "disconnected" );
			client_close( c );

			RLAST( "231", "disconnected" );
			return;
		}
	}

	RLAST( "530", "session not found" );
}

/************************************************************
 * commands: user + auth
 */

static void proto_bcast_login( t_client *client )
{
	char buf[BUFLENWHO];

	proto_bcast( r_user, "630", "%s", mkclient(buf, BUFLENWHO, client));
}

static void proto_bcast_logout( t_client *client )
{
	char buf[BUFLENWHO];

	proto_bcast( r_user, "631", "%s", mkclient(buf, BUFLENWHO, client));
}

CMD(cmd_user, r_any, p_open, arg_need )
{
	client->pdata = strdup( line );
	client->pstate = p_user;

	RLAST( "320", "user ok, use PASS for password" );
}

CMD(cmd_pass, r_any, p_user, arg_need )
{
	if( ! user_ok( client->pdata, line )){
		client->pstate = p_open;
		client->right = r_any;
		client->uid = 0;
		RLAST("52x", "login failed" );
	
	} else {
		client->uid = 1;
		client->pstate = p_idle;
		client->right = r_master;

		syslog( LOG_INFO, "user %s logged in", (char*)client->pdata );
		RLAST( "221", "successfully logged in" );
		proto_bcast_login(client);
	}

	free( client->pdata );
	client->pdata = NULL;
}

CMD(cmd_who, r_user, p_idle, arg_none )
{
	char buf[BUFLENWHO];
	t_client *c;

	(void)line;
	for( c = clients; c; c = c->next ){
		if( c->close )
			continue;

		RLINE( "230", "%s", mkclient(buf, BUFLENWHO, c));
	}
	RLAST( "230", "");
}

CMD(cmd_kick, r_master, p_idle, arg_need )
{
	int uid;
	char *end;
	t_client *c;
	int found = 0;

	uid = strtol( line, &end, 10 );
	if( *end ){
		RBADARG( "invalid user ID" );
		return;
	}

	for( c = clients; c; c = c->next ){
		if( c->uid == uid ){
			proto_rlast( c, "632", "kicked" );
			client_close( c );

			found++;
		}
	}

	if( found )
		RLINE( "231", "kicked" );
	else 
		RLAST( "530", "user not found" );
}

// TODO: cmd_users, r_user },
// TODO: cmd_userget, r_user },


/************************************************************
 * commands: player
 */

static void proto_bcast_player_newtrack( void )
{
	char buf[BUFLENTRACK];
	t_track *track;

	track = player_track();
	proto_bcast( r_guest, "640", "%s", mktrack(buf, BUFLENTRACK, track) );
	track_free(track);
}

static void proto_bcast_player_stop( void )
{
	proto_bcast( r_guest, "641", "stopped" );
}

static void proto_bcast_player_pause( void )
{
	proto_bcast( r_guest, "642", "paused" );
}

static void proto_bcast_player_resume( void )
{
	proto_bcast( r_guest, "643", "resumed" );
}

static void proto_bcast_player_random( void )
{
	proto_bcast( r_guest, "646", "%d", player_random() );
}

static void reply_player( t_client *client, int r )
{
	switch(r){ 
		case PE_OK: 
			break; 

		case PE_NOTHING:
			RLAST("541", "nothing to do" );
			return;

		case PE_BUSY:
			RLAST("541", "already doing this" );
			return;

		case PE_NOSUP:
			RLAST("541", "not supported" );
			return;

		case PE_SYS:
			RLAST("540", "player error: %s", STRERR);
			return;

		default:
			RLAST( "541", "player error" );
			return;
	}
}
#define RPLAYER(x)	reply_player(client,x)


CMD(cmd_play, r_user, p_idle, arg_none )
{
	int r;

	(void)line;
	r = player_start();
	if( r == PE_OK ){
		RLAST( "240", "playing" );
		return;
	}

	RPLAYER(r);
}

CMD(cmd_stop, r_user, p_idle, arg_none )
{
	int r;

	(void)line;
	r = player_stop();
	if( r == PE_OK ){
		RLAST( "241", "stopped" );
		return;
	}

	RPLAYER(r);
}

CMD(cmd_next, r_user, p_idle, arg_none )
{
	int r;

	(void)line;
	r = player_next();
	if( r == PE_OK  ){
		RLAST( "240", "playing" );
		return;
	}

	RPLAYER(r);
}

CMD(cmd_prev, r_user, p_idle, arg_none )
{
	int r;

	(void)line;
	r = player_prev();
	if( r == PE_OK ){
		RLAST( "240", "playing" );
		return;
	}

	RPLAYER(r);
}

CMD(cmd_pause, r_user, p_idle, arg_none )
{
	int r;

	(void)line;
	r = player_pause();
	if( r == PE_OK ){
		RLAST( "242", "paused" );
		return;
	}

	RPLAYER(r);
}

CMD(cmd_status, r_guest, p_idle, arg_none )
{
	(void)line;

	RLAST( "243", "%d", player_status() );
}

CMD(cmd_curtrack, r_guest, p_idle, arg_none )
{
	char buf[BUFLENTRACK];
	t_track *t;

	(void)line;

	if( ! (t = player_track())){
		RLAST("541", "nothing playing (maybe in a gap?)" );
		return;
	}

	RLAST( "248", "%s", mktrack(buf, BUFLENTRACK, t) );
	track_free(t);
}

CMD(cmd_gap, r_guest, p_idle, arg_none )
{
	(void)line;
	RLAST( "244", "%d", player_gap() );
}

CMD(cmd_gapset, r_user, p_idle, arg_need )
{
	char *end;
	int gap;

	gap = strtol( line, &end, 10 );
	if( *end ){
		RBADARG( "invalid time" );
		return;
	}

	player_setgap( gap );
	RLAST( "245", "gap adjusted" );
}

CMD(cmd_random, r_guest, p_idle, arg_none )
{
	(void)line;
	RLAST( "246", "%d", player_random() );
}

CMD(cmd_randomset, r_user, p_idle, arg_need )
{
	int r;
	char *end;

	r = strtol( line, &end, 10 );
	if( *end ){
		RBADARG( "invalid bool" );
		return;
	}
			
	player_setrandom( r );
	RLAST( "247", "random adjusted" );
}

/************************************************************
 * commands: sleep 
 */

static void proto_bcast_sleep( void )
{
	proto_bcast( r_guest, "651", "%d", sleep_remain());
}


CMD(cmd_sleep, r_guest, p_idle, arg_none )
{
	(void)line;
	RLAST( "215", "%d", sleep_remain() );
}

CMD(cmd_sleepset, r_user, p_idle, arg_need )
{
	int sec;
	char *end;

	sec = strtol( line, &end, 10 );
	if( *end ){
		RBADARG( "invalid time" );
		return;
	}

	sleep_in( sec );
	RLAST( "216", "ok, will stop in %d seconds", sec);
}


/************************************************************
 * commands: track 
 */


CMD(cmd_trackget, r_guest, p_idle, arg_need )
{
	char buf[BUFLENTRACK];
	char *end;
	int id;
	t_track *t;

	id = strtol(line, &end, 10);
	if( *end ){
		RBADARG( "expecting only a track ID");
		return;
	}

	if( ! (t = track_get( id ))){
		RLAST("511", "no such track" );
		return;
	}

	RLAST( "210", "%s", mktrack(buf, BUFLENTRACK, t) );
	track_free(t);
}

static void dump_tracks( t_client *client, const char *code, it_track *it )
{
	char buf[BUFLENTRACK];
	t_track *t;

	for( t = it_track_begin(it); t; t = it_track_next(it) ){
		RLINE(code,"%s", mktrack(buf, BUFLENTRACK, t) );
		track_free(t);
	}

	RLAST(code, "" );
}

CMD(cmd_tracks, r_guest, p_idle, arg_none )
{
	int matches;

	(void)line;
	if( 0 > ( matches = tracks())){
		RLAST( "510", "internal error" );
		return;
	}

	RLAST( "214", "%d", matches );
}

CMD(cmd_tracksearch, r_guest, p_idle, arg_need )
{
	it_track *it;

	it = tracks_search(line);
	dump_tracks( client, "211", it );
	it_track_done(it);
}

CMD(cmd_tracksalbum, r_guest, p_idle, arg_need )
{
	char *end;
	int id;
	it_track *it;

	id = strtol(line, &end, 10);
	if( *end ){
		RBADARG( "expecting only an album ID");
		return;
	}

	it = tracks_albumid(id);
	dump_tracks( client, "212", it );
	it_track_done(it);
}

CMD(cmd_tracksartist, r_guest, p_idle, arg_need )
{
	char *end;
	int id;
	it_track *it;

	id = strtol(line, &end, 10);
	if( *end ){
		RBADARG( "expecting only an artist ID");
		return;
	}

	it = tracks_artistid(id);
	dump_tracks( client, "213", it );
	it_track_done(it);
}

CMD(cmd_trackid, r_guest, p_idle, arg_need )
{
	char *s;
	char *e;
	int albumid;
	int num;
	int id;

	s = line;
	albumid = strtol(s, &e, 10 );
	if( s == e ){
		RBADARG( "expecting an album ID" );
		return;
	}

	s = e + strspn(e, "\t ");
	num = strtol(s, &e, 10 );
	if( s==e || *e ){
		RBADARG( "expecting a title number for this album" );
		return;
	}

	if( 0 > (id = track_id( albumid, num))){
		RBADARG( "no such track found" );
		return;
	}

	RLAST( "214", "%d", id );
}


CMD(cmd_trackalter, r_user, p_idle, arg_need )
{
	(void)line;
	RBADARG("TODO: trackalter");
}

/************************************************************
 * commands: random 
 */

static void proto_bcast_filter( void )
{
	const char *f;

	f = random_filter();
	proto_bcast( r_guest, "650", "%s", f ? f : "" );
}


CMD(cmd_filter, r_guest, p_idle, arg_none )
{
	const char *f;

	(void)line;
	f = random_filter();
	RLAST( "250", "%s", f ? f : "" );
}

CMD(cmd_filterset, r_user, p_idle, arg_opt )
{
	if( random_setfilter(line)){
		RLAST( "511", "invalid filter" );
		return;
	}

	RLAST( "251", "filter changed" );
}

CMD(cmd_filterstat, r_guest, p_idle, arg_none )
{
	int matches;

	(void)line;
	if( 0 > ( matches = random_filterstat())){
		RLAST( "550", "filter error" );
		return;
	}

	RLAST( "253", "%d", matches );
}

CMD(cmd_randomtop, r_guest, p_idle, arg_opt )
{
	int num;
	char *end;
	it_track *it;

	if( *line ){
		num = strtol(line, &end, 10);
		if( *end ){
			RBADARG( "expecting only a number");
			return;
		}
	} else {
		num = 20;
	}

	it = random_top(num);
	dump_tracks( client, "252", it );
	it_track_done(it);
}


/************************************************************
 * commands: history
 */

static void dump_history( t_client *client, const char *code, it_history *it )
{
	char buf[BUFLENTRACK];
	t_history *t;

	for( t = it_history_begin(it); t; t = it_history_next(it) ){
		RLINE(code,"%s", mkhistory(buf, BUFLENTRACK, t) );
		history_free(t);
	}

	RLAST(code, "" );
}

CMD(cmd_history, r_guest, p_idle, arg_opt )
{
	int num = 20;
	char *end;
	it_history *it;

	if( *line ){
		num = strtol( line, &end, 10 );
		if( *end ){
			RBADARG( "expecting a number" );
			return;
		}
	}

	it = history_list( num );
	dump_history( client, "260", it );
	it_history_done(it);
}

CMD(cmd_historytrack, r_guest, p_idle, arg_need )
{
	int id;
	int num = 20;
	char *end1;
	char *end2;
	it_history *it;

	/* first get the mandatory title id */
	id = strtol( line, &end1, 10 );
	if( end1 == line ){
		RBADARG( "expecting a title ID" );
		return;
	}

	/* skip the seperating whitespaces */
	while( *end1 && isspace(*end1) )
		end1++;

	/* and optionally get the number of entries to display */
	if( *end1 ){
		num = strtol( end1, &end2, 10 );
		if( end1 == end2 ){
			RBADARG( "expecting a number" );
			return;
		}
	}

	it = history_tracklist( id, num );
	dump_history( client, "260", it );
	it_history_done(it);
}


/************************************************************
 * commands: queue
 */

static void proto_bcast_queue_fetch( t_queue *q )
{
	char buf[BUFLENTRACK];
	proto_bcast( r_guest, "660", "%s", mkqueue(buf,BUFLENTRACK,q) );
}

static void proto_bcast_queue_add( t_queue *q )
{
	char buf[BUFLENTRACK];
	proto_bcast( r_guest, "661", "%s", mkqueue(buf,BUFLENTRACK,q) );
}

static void proto_bcast_queue_del( t_queue *q )
{
	char buf[BUFLENTRACK];
	proto_bcast( r_guest, "662", "%s", mkqueue(buf,BUFLENTRACK,q));
}

static void proto_bcast_queue_clear( void )
{
	proto_bcast( r_guest, "663", "queue cleared" );
}

CMD(cmd_queue, r_guest, p_idle, arg_none)
{
	char buf[BUFLENTRACK];
	it_queue *it;
	t_queue *q;

	(void)line;
	it = queue_list();
	for( q = it_queue_begin(it); q; q = it_queue_next(it)){
		RLINE("260", "%s", mkqueue(buf, BUFLENTRACK, q ));
		queue_free(q);
	}
	it_queue_done(it);
	RLAST("260","");
}

CMD(cmd_queueadd, r_user, p_idle, arg_need)
{
	int id;
	char *end;
	int qid;

	id = strtol( line, &end, 10 );
	if( *end ){
		RBADARG( "expecting a track ID" );
		return;
	}

	if( -1 == (qid = queue_add(id, client->uid))){
		RLAST( "561", "failed to add track to queue" );
		return;
	}

	RLAST( "261", "%d", qid );
}

CMD(cmd_queuedel, r_user, p_idle, arg_need)
{
	int id;
	char *end;
	int uid;

	id = strtol( line, &end, 10 );
	if( *end ){
		RBADARG( "expecting a queue ID" );
		return;
	}

	uid = client->uid;
	if( client->right == r_master )
		uid = 0;

	if( queue_del( id, uid )){
		RLAST("562", "failed to delete from queue" );
		return;
	}

	RLAST("262", "track removed from queue" );
}

CMD(cmd_queueclear, r_master, p_idle, arg_none)
{
	(void)line;
	if( queue_clear() ){
		RLAST("563", "failed to clear queue" );
		return;
	}

	RLAST("263", "queue cleared" );
}

CMD(cmd_queueget, r_user, p_idle, arg_need)
{
	char buf[BUFLENTRACK];
	int id;
	char *end;
	t_queue *q;

	id = strtol( line, &end, 10 );
	if( *end ){
		RBADARG( "ecpecting a queue ID" );
		return;
	}

	if( NULL == (q = queue_get(id))){
		RBADARG( "queue entry not found" );
		return;
	}

	RLAST( "264", "%s", mkqueue(buf,BUFLENTRACK, q ));
	queue_free(q);
}

/************************************************************
 * commands: tag
 */

static void proto_bcast_tag_changed( t_tag *t )
{
	char buf[BUFLENTAG];
	proto_bcast( r_guest, "670", "%s", mktag(buf,BUFLENTAG,t));
}

static void proto_bcast_tag_del( t_tag *t )
{
	char buf[BUFLENTAG];
	proto_bcast( r_guest, "671", "%s", mktag(buf,BUFLENTAG,t));
}

static void dump_tags( t_client *client, const char *code, it_tag *it )
{
	char buf[BUFLENTAG];
	t_tag *t;

	for( t = it_tag_begin(it); t; t = it_tag_next(it) ){
		RLINE(code,"%s", mktag(buf, BUFLENTAG, t) );
		tag_free(t);
	}

	RLAST(code, "" );
}

CMD(cmd_taglist, r_guest, p_idle, arg_none )
{
	it_tag *it;

	(void)line;
	it = tags_list();
	dump_tags( client, "270", it );
	it_tag_done(it );
}

CMD(cmd_tagget, r_guest, p_idle, arg_need )
{
	char buf[BUFLENTAG];
	t_tag *t;
	char *end;
	int id;

	id = strtol( line, &end, 10 );
	if( *end ){
		RBADARG( "ecpecting a tag ID" );
		return;
	}

	if( NULL == (t = tag_get( id ))){
		RLAST("511", "no such tag" );
		return;
	}

	RLAST("271", "%s", mktag(buf, BUFLENTAG, t));
}

CMD(cmd_tagname, r_guest, p_idle, arg_need )
{
	char buf[BUFLENTAG];
	t_tag *t;

	if( NULL == (t = tag_getname( line ))){
		RLAST("511", "no such tag" );
		return;
	}
	RLAST("272", "%s", mktag(buf, BUFLENTAG, t));
}

CMD(cmd_tagadd, r_guest, p_idle, arg_need )
{
	int id;

	if( 0 > (id = tag_add( line ))){
		RLAST("511", "failed" );
		return;
	}

	RLAST( "273", "%d", id );
}

CMD(cmd_tagsetname, r_guest, p_idle, arg_need )
{
	char *end;
	int id;

	id = strtol( line, &end, 10 );
	if( line == end ){
		RBADARG( "ecpecting a tag ID" );
		return;
	}

	end += strspn(end, " \t" );
	if( tag_setname(id, end )){
		RLAST("511", "failed" );
		return;
	}

	RLAST("274", "name changed" );
}

CMD(cmd_tagsetdesc, r_guest, p_idle, arg_need )
{
	char *end;
	int id;

	id = strtol( line, &end, 10 );
	if( line == end ){
		RBADARG( "ecpecting a tag ID" );
		return;
	}

	end += strspn(end, " \t" );
	if( tag_setdesc(id, end )){
		RLAST("511", "failed" );
		return;
	}

	RLAST("275", "desc changed" );
}

CMD(cmd_tagdel, r_user, p_idle, arg_need )
{
	char *end;
	int id;

	id = strtol( line, &end, 10 );
	if( *end ){
		RBADARG( "ecpecting a tag ID" );
		return;
	}

	if( tag_del( id )){
		RLAST("511", "failed" );
		return;
	}

	RLAST("276", "deleted" );
}

CMD(cmd_tracktags, r_guest, p_idle, arg_need )
{
	it_tag *it;
	char *end;
	int id;

	id = strtol( line, &end, 10 );
	if( line == end ){
		RBADARG( "ecpecting a tag ID" );
		return;
	}

	it = track_tags(id);
	dump_tags( client, "277", it );
	it_tag_done(it );
}

CMD(cmd_tracktagset, r_guest, p_idle, arg_need )
{
	char *s, *e;
	int trackid;
	int tagid;

	trackid = strtol(line, &e, 10 );
	if( line == e ){
		RBADARG( "missing/invalid track id");
		return;
	}

	s = e + strspn(e, "\t " );
	tagid = strtol(s, &e, 10 );
	if( *e ){
		RBADARG( "missing/invalid tag id");
		return;
	}

	if( track_tagset(trackid,tagid)){
		RLAST("511", "failed" );
		return;
	}

	RLAST("278", "tag added to track (or already exists)" );
}

CMD(cmd_tracktagdel, r_guest, p_idle, arg_need )
{
	char *s, *e;
	int trackid;
	int tagid;

	trackid = strtol(line, &e, 10 );
	if( line == e ){
		RBADARG( "missing/invalid track id");
		return;
	}

	s = e + strspn(e, "\t " );
	tagid = strtol(s, &e, 10 );
	if( *e ){
		RBADARG( "missing/invalid tag id");
		return;
	}

	if( track_tagdel(trackid,tagid)){
		RLAST("511", "failed" );
		return;
	}

	RLAST("279", "tag deleted from track" );
}

CMD(cmd_tracktagged, r_guest, p_idle, arg_need )
{
	char *s, *e;
	int trackid;
	int tagid;
	int r;

	trackid = strtol(line, &e, 10 );
	if( line == e ){
		RBADARG( "missing/invalid track id");
		return;
	}

	s = e + strspn(e, "\t " );
	tagid = strtol(s, &e, 10 );
	if( *e ){
		RBADARG( "missing/invalid tag id");
		return;
	}

	if( 0 > (r = track_tagged(trackid,tagid))){
		RLAST("511", "failed" );
		return;
	}

	RLAST("279", "%d", r );
}


/************************************************************
 * command array
 */

CMD(cmd_help, r_any, p_any, arg_none );

typedef void (*t_func_cmd)( t_client *client, char *line );

typedef struct _t_cmd {
	const char *name;
	t_func_cmd cmd;
	t_rights right;
	t_protstate state;
	t_args args;
} t_cmd;

static t_cmd proto_cmds[] = {
#include "cmd.list"
	{ NULL, NULL, 0, 0, 0 }
};


static void cmd_help( t_client *client, char *line )
{
	t_cmd *c;

	(void)line;
	for( c = proto_cmds; c && c->name; ++c ){
		if( client->pstate != c->state && c->state != p_any )
			continue;
		
		if( client->right < c->right )
			continue;

		RLINE("219", c->name );
	}
	RLAST( "219", "" );
}

/*
 * find apropriate command entry
 * check permissions
 * strip command from line leaving only args
 * and pass args to apropriate function
 */
static void cmd( t_client *client, char *line )
{
	unsigned int len;
	t_cmd *c;
	char *s;

	len = strcspn( line, " \t" );
	for( c = proto_cmds; c && c->name; c++ ){
		if( len != strlen(c->name) )
			continue;
		if( 0 != strncasecmp(c->name, line, len ))
			continue;

		switch( c->state ){
			case p_any:
				break;

			case p_open:
				if( client->pstate == p_open )
					break;
				RLAST( "520", "you are already authenticated");
				return;

			case p_user:
				if( client->pstate == p_user )
					break;
				RLAST( "520", "fisrt issue a USER command" );
				return;

			default:
				if( client->pstate == c->state )
					break;
				RLAST( "520", "different command required" );
				return;
		}

		if( c->right > client->right ){
			RLAST( "520", "permission denied");
			return;
		}

		s = line + len;
		while( *s && isspace(*s) )
			s++;

		switch( c->args ){
			case arg_none:
				if( ! *s )
					break;
				RBADARG( "command takes no arguments" );
				return;

			case arg_need:
				if( *s )
					break;
				RBADARG( "command needs arguments" );
				return;

			default:
				break;
		}


		(*c->cmd)(client, s);
		return;
	}

	RLAST( "500", "unkonwn command" );
}


/*
 * initialize protocol for a newly connected client
 */
static void proto_newclient( t_client *client )
{
	RLAST( "220", "hello" );
	syslog( LOG_DEBUG, "new connection %d from %s", client->id,
			inet_ntoa(client->sin.sin_addr ));
}

/*
 * cleanup proto when a client disconnected
 */
static void proto_delclient( t_client *client )
{
	syslog( LOG_DEBUG, "lost connection %d to %s", client->id,
			inet_ntoa(client->sin.sin_addr ));

	if( client->pstate != p_open )
		proto_bcast_logout( client );
}

/************************************************************
 * interface functions
 */

/*
 * initialize protocol
 */

void proto_init( void )
{
	client_func_connect = proto_newclient;
	client_func_disconnect = proto_delclient;

	player_func_newtrack = proto_bcast_player_newtrack;
	player_func_pause = proto_bcast_player_pause;
	player_func_resume = proto_bcast_player_resume;
	player_func_stop = proto_bcast_player_stop;
	player_func_random = proto_bcast_player_random;

	sleep_func_set = proto_bcast_sleep;

	random_func_filter = proto_bcast_filter;

	queue_func_add = proto_bcast_queue_add;
	queue_func_del = proto_bcast_queue_del;
	queue_func_clear = proto_bcast_queue_clear;
	queue_func_fetch = proto_bcast_queue_fetch;

	tag_func_changed = proto_bcast_tag_changed;
	tag_func_del = proto_bcast_tag_del;
}


/*
 * process each incoming line
 */
void proto_input( t_client *client )
{
	char *line;

	while( NULL != (line = client_getline( client) )){
		int l = strlen(line);

		/* strip trailing whitespace */
		while( --l >= 0 && isspace(line[l]) ){
			line[l] = 0;
		}
			
		if( l >= 0 )
			cmd( client, line );
		else
			RLAST( "500", "no command" );

		free(line);
	} 
}


