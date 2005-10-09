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
 * x8z: album
 * x9z: artist
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

#include <config.h>
#include "commondb/album.h"
#include "commondb/artist.h"
#include "commondb/user.h"
#include "commondb/history.h"
#include "commondb/random.h"
#include "commondb/queue.h"
#include "commondb/tag.h"
#include "commondb/sfilter.h"
#include "player.h"
#include "sleep.h"
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
#define BUFLENUSER	512
#define BUFLENALBUM	512
#define BUFLENARTIST	512

static char *proto_fmtline( int last, const char *code, 
		const char *fmt, va_list ap )
{
	char buffer[BUFLENLINE];
	int r;

	sprintf( buffer, "%3.3s%c", code, last ? ' ' : '-' );

	r = vsnprintf( buffer + 4, BUFLENLINE - 5, fmt, ap );
	if( r < 0 || r > BUFLENLINE - 5 )
		return NULL;

	r+=4;
	buffer[r++] = '\n';
	buffer[r++] = 0;
	return strdup(buffer);
}

static int proto_vline( t_client *client, int last, const char *code, 
		const char *fmt, va_list ap )
{
	char *line;

	if( NULL == ( line = proto_fmtline( last, code, fmt, ap )))
		return -1;

	client_send( client, line );
	free(line);

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
	char *line;
	va_list ap;

	va_start(ap, fmt);
	if( NULL != (line = proto_fmtline(1, code, fmt, ap))){
		client_bcast_perm(line, right);
		free(line);
	}
	va_end(ap);
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
			int c = va_arg( ap, int );
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

static inline int mkuser( char *buf, int len, t_user *u )
{
	if( ! u )
		return mktab(buf, len, "dsd", 0, "UNKNOWN", p_any );

	return mktab(buf, len, "dsd", u->id, u->name, u->right );
}

/* make a reply string from client data */
static inline int mkclient( char *buf, int len, t_client *c )
{
	int used;
	
	used = mktab( buf, len, "ds",
			c->id, inet_ntoa(c->sin.sin_addr));
	if(len > used++ ){ buf[used-1] = '\t'; }
	used += mkuser( buf+used, len-used, c->user );

	return used;
}

static inline int mktag( char *buf, int len, t_tag *t )
{
	return mktab(buf, len, "dss", t->id, t->name, t->desc );
}

static inline int mkartist( char *buf, int len, t_artist *a )
{
	return mktab(buf, len, "ds", a->id, a->artist );
}

static inline int mkalbum( char *buf, int len, t_album *a )
{
	int used;
	
	used = mktab(buf, len, "ds", a->id, a->album );
	if(len > used++ ){ buf[used-1] = '\t'; }
	used += mkartist( buf+used, len-used, a->artist );

	return used;
}

static inline int mktrack( char *buf, int len, t_track *t )
{
	int used;
	
	used = mktab( buf, len, "ddsd", 
				t->id, 
				t->albumnr, 
				t->title, 
				t->duration);
	if(len > used++ ){ buf[used-1] = '\t'; }
	used += mkartist( buf+used, len-used, t->artist );
	if(len > used++ ){ buf[used-1] = '\t'; }
	used += mkalbum( buf+used, len-used, t->album );

	return used;
}

static inline int mkhistory( char *buf, int len, t_history *h )
{
	int used;

	used = mktab( buf, len, "d",
				h->played);
	if(len > used++ ){ buf[used-1] = '\t'; }
	used += mkuser( buf+used, len-used, h->user );
	if(len > used++ ){ buf[used-1] = '\t'; }
	used += mktrack( buf+used, len-used, h->track );
	return used;
}

static inline int mkqueue( char *buf, int len, t_queue *q )
{
	int used;

	used = mktab(buf, len, "dd",
				q->id,
				q->queued);
	if(len > used++ ){ buf[used-1] = '\t'; }
	used += mkuser( buf+used, len-used, q->user );
	if(len > used++ ){ buf[used-1] = '\t'; }
	used += mktrack( buf+used, len-used, q->track );
	return used;
}

static inline int mksfilter( char *buf, int len, t_sfilter *t )
{
	return mktab(buf, len, "dss", t->id, t->name, t->filter );
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

CMD(cmd_clientclose, r_master, p_idle, arg_need )
{
	int id;
	char *end;
	t_client *c;

	id = strtol( line, &end, 10 );
	if( *end ){
		RBADARG( "invalid session ID" );
		return;
	}

	if( NULL == (c = client_get( id ))){
		RLAST( "530", "session not found" );
		return;
	}

	proto_rlast( c, "632", "disconnected" );
	client_close( c );
	client_delref(c);

	RLAST( "232", "disconnected" );
}

/************************************************************
 * commands: user + auth
 */

static void proto_bcast_login( t_client *client )
{
	char buf[BUFLENWHO];

	mkclient(buf, BUFLENWHO, client);
	proto_bcast( r_user, "630", "%s", buf );
}

static void proto_bcast_logout( t_client *client )
{
	char buf[BUFLENWHO];

	mkclient(buf, BUFLENWHO, client);
	proto_bcast( r_user, "631", "%s", buf ); 
}

CMD(cmd_user, r_any, p_open, arg_need )
{
	if( client->pdata )
		free(client->pdata);

	client->pdata = strdup( line );
	client->pstate = p_user;

	RLAST( "320", "user ok, use PASS for password" );
}

CMD(cmd_pass, r_any, p_user, arg_need )
{
	if( NULL == ( client->user = user_getn( client->pdata )))
		goto clean1;

	if( ! user_ok( client->user, line ))
		goto clean2;

	client->pstate = p_idle;

	syslog( LOG_INFO, "con #%d: user %s logged in", 
			client->id, client->user->name );
	RLAST( "221", "successfully logged in" );
	proto_bcast_login(client);

	goto final;

clean2:
	user_free(client->user);
	client->user = NULL;

clean1:
	client->pstate = p_open;

	syslog( LOG_NOTICE, "con #%d: login failed", client->id );
	RLAST("521", "login failed" );

final:
	free( client->pdata );
	client->pdata = NULL;
}

CMD(cmd_clientlist, r_user, p_idle, arg_none )
{
	char buf[BUFLENWHO];
	it_client *it;
	t_client *t;

	(void)line;
	it = clients_list();
	for( t = it_client_begin(it); t; t = it_client_next(it) ){
		mkclient(buf, BUFLENWHO, t);
		RLINE( "230", "%s", buf ); 
		client_delref(t);
	}
	RLAST( "230", "");
	it_client_done(it);
}

CMD(cmd_clientcloseuser, r_master, p_idle, arg_need )
{
	int uid;
	char *end;
	it_client *it;
	t_client *t;
	int found = 0;

	uid = strtol( line, &end, 10 );
	if( *end ){
		RBADARG( "invalid user ID" );
		return;
	}

	if( NULL == (it = clients_uid(uid))){
		return;
	}

	for( t = it_client_begin(it); t; t = it_client_next(it) ){
		proto_rlast( t, "632", "kicked" );
		client_close( t );
		client_delref(t);
		found++;
	}

	if( found )
		RLAST( "231", "kicked" );
	else
		RLAST( "530", "user not found" );
	it_client_done(it);
}

CMD(cmd_userget, r_user, p_idle, arg_need )
{
	int uid;
	char *end;
	t_user *u;
	char buf[BUFLENUSER];

	uid = strtol(line, &end, 10 );
	if( *end ){
		RBADARG("invalid user ID" );
		return;
	}

	if( NULL == (u = user_get(uid))){
		RBADARG("no such user");
		return;
	}

	mkuser(buf, BUFLENUSER, u );
	RLAST( "233", "%s", buf ); 
	user_free(u);
}


CMD(cmd_user2id, r_user, p_idle, arg_need )
{
	int uid;

	if( 0 > ( uid = user_id(line))){
		RBADARG("no such user");
		return;
	}

	RLAST("234", "%d", uid);
}

static void dump_users( t_client *client, const char *code, it_user *it )
{
	char buf[BUFLENUSER];
	t_user *t;

	for( t = it_user_begin(it); t; t = it_user_next(it) ){
		mkuser(buf, BUFLENUSER, t);
		RLINE(code,"%s", buf ); 
		user_free(t);
	}

	RLAST(code, "" );
}

CMD(cmd_userlist, r_user, p_idle, arg_none )
{
	it_user *it;

	(void)line;
	it = users_list();
	dump_users( client, "235", it );
	it_user_done( it );
}

CMD(cmd_usersetpass, r_master, p_idle, arg_need )
{
	int uid;
	char *end;
	t_user *u;

	uid = strtol(line, &end, 10 );
	if( line == end ){
		RBADARG("invalid user ID" );
		return;
	}

	if( NULL == (u = user_get(uid))){
		RBADARG("no such user");
		return;
	}

	end += strspn( end, " \t");

	if( 0 > user_setpass(u, end) ){
		RLAST( "530", "failed");
	} else {
		RLAST( "236", "password changed" );
		user_save(u);
	}
	user_free(u);
}

CMD(cmd_usersetright, r_master, p_idle, arg_need )
{
	int uid, right;
	char *end, *s;
	t_user *u;

	uid = strtol(line, &end, 10 );
	if( line == end ){
		RBADARG("invalid user ID" );
		return;
	}

	s = end + strspn( end, " \t");
	right = strtol( s, &end, 10 );
	if( *end ){
		RBADARG( "invalid right number" );
		return;
	}

	if( NULL == (u = user_get(uid))){
		RBADARG("no such user");
		return;
	}


	if( 0 > user_setright(u, right) ){
		RLAST( "530", "failed");
	} else {
		RLAST( "237", "right changed" );
		user_save(u);
	}
	user_free(u);
}

CMD(cmd_useradd, r_master, p_idle, arg_need )
{
	int uid;

	if( 0 > ( uid = user_add(line, 1, ""))){
		RLAST( "530", "failed" );
		return;
	} 

	RLAST( "238", "%d", uid );
}

CMD(cmd_userdel, r_master, p_idle, arg_need )
{
	int uid;
	char *end;

	uid = strtol(line, &end, 10 );
	if( *end ){
		RBADARG("invalid user ID" );
		return;
	}

	if( 0>  user_del(uid)){
		RLAST( "530", "failed" );
		return;
	}

	RLAST( "239", "deleted");
}



/************************************************************
 * commands: player
 */

static void proto_bcast_player_newtrack( void )
{
	char buf[BUFLENTRACK];
	t_track *track;

	if( NULL == (track = player_track() ))
		return;

	mktrack(buf, BUFLENTRACK, track);
	proto_bcast( r_guest, "640", "%s", buf ); 
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

static void proto_bcast_player_elapsed( void )
{
	proto_bcast( r_guest, "646", "%d", player_elapsed() );
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

CMD(cmd_elapsed, r_guest, p_idle, arg_none )
{
	(void)line;
	RLAST( "249", "%d", player_elapsed() );
}

CMD(cmd_jump, r_user, p_idle, arg_need )
{
	char *end;
	int to_sec;
	int r;

	to_sec = strtol( line, &end, 10 );
	if( *end ){
		RBADARG( "invalid time" );
		return;
	}

	if( PE_OK == (r = player_jump(to_sec))){
		RLAST("248", "jumped" );
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


	mktrack(buf, BUFLENTRACK, t);
	RLAST( "248", "%s", buf );
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

	mktrack(buf, BUFLENTRACK, t);
	RLAST( "210", "%s", buf ); 
	track_free(t);
}

static void dump_tracks( t_client *client, const char *code, it_track *it )
{
	char buf[BUFLENTRACK];
	t_track *t;

	for( t = it_track_begin(it); t; t = it_track_next(it) ){
		mktrack(buf, BUFLENTRACK, t);
		RLINE(code,"%s", buf ); 
		track_free(t);
	}

	RLAST(code, "" );
}

CMD(cmd_trackcount, r_guest, p_idle, arg_none )
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

CMD(cmd_tracksearchf, r_guest, p_idle, arg_need )
{
	expr *e = NULL;
	char *msg;
	int pos;
	it_track *it;

	if( NULL == (e = expr_parse_str( &pos, &msg, line ))){
		RLAST( "511", "error at pos %d in filter: %s", pos, msg );
		return;
	}

	it = tracks_searchf( e );
	dump_tracks( client, "211", it );
	it_track_done(it);

	expr_free(e);
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

CMD(cmd_track2id, r_guest, p_idle, arg_need )
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


CMD(cmd_trackalter, r_admin, p_idle, arg_need )
{
	(void)line;
	RBADARG("TODO: trackalter");
}

/************************************************************
 * commands: random 
 */

static void proto_bcast_filter( void )
{
	char buf[1024];
	expr *e;

	e = random_filter();
	if( e )
		expr_fmt( buf, 1024, e );

	proto_bcast( r_guest, "650", "%s", e ? buf : "" );
}


CMD(cmd_filter, r_guest, p_idle, arg_none )
{
	char buf[1024];
	expr *e;

	(void)line;
	e = random_filter();
	if( e )
		expr_fmt( buf, 1024, e );

	RLAST( "250", "%s", e ? buf : "" );
}

CMD(cmd_filterset, r_user, p_idle, arg_opt )
{
	expr *e = NULL;
	char *msg;
	int pos;

	if( line && *line ){
		if( NULL == (e = expr_parse_str( &pos, &msg, line ))){
			RLAST( "511", "error at pos %d in filter: %s",
					pos, msg );
			return;
		}
	}

	/* at least initialize with an empty filter */
	if( random_setfilter(e)){
		expr_free(e);
		RLAST( "511", "failed to apply (correct) filter" );
		return;
	}

	expr_free(e);
	RLAST( "251", "filter changed" );
}

CMD(cmd_filterstat, r_guest, p_idle, arg_none )
{
	int matches;

	(void)line;
	if( 0 > ( matches = random_filterstat())){
		RLAST( "511", "filter error" );
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
		mkhistory(buf, BUFLENTRACK, t);
		RLINE(code,"%s", buf ); 
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

	mkqueue(buf,BUFLENTRACK,q);
	proto_bcast( r_guest, "660", "%s", buf ); 
}

static void proto_bcast_queue_add( t_queue *q )
{
	char buf[BUFLENTRACK];

	mkqueue(buf,BUFLENTRACK,q);
	proto_bcast( r_guest, "661", "%s", buf ); 
}

static void proto_bcast_queue_del( t_queue *q )
{
	char buf[BUFLENTRACK];

	mkqueue(buf,BUFLENTRACK,q);
	proto_bcast( r_guest, "662", "%s", buf ); 
}

static void proto_bcast_queue_clear( void )
{
	proto_bcast( r_guest, "663", "queue cleared" );
}

CMD(cmd_queuelist, r_guest, p_idle, arg_none)
{
	char buf[BUFLENTRACK];
	it_queue *it;
	t_queue *q;

	(void)line;
	it = queue_list();
	for( q = it_queue_begin(it); q; q = it_queue_next(it)){
		mkqueue(buf, BUFLENTRACK, q );
		RLINE("260", "%s", buf ); 
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

	if( -1 == (qid = queue_add(id, client->user->id))){
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

	uid = client->user->id;
	if( client->user->right == r_master )
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

	mkqueue(buf,BUFLENTRACK, q );
	RLAST( "264", "%s", buf ); 
	queue_free(q);
}

/************************************************************
 * commands: tag
 */

static void proto_bcast_tag_changed( t_tag *t )
{
	char buf[BUFLENTAG];
	mktag(buf,BUFLENTAG,t);
	proto_bcast( r_guest, "670", "%s", buf ); 
}

static void proto_bcast_tag_del( t_tag *t )
{
	char buf[BUFLENTAG];
	mktag(buf,BUFLENTAG,t);
	proto_bcast( r_guest, "671", "%s", buf ); 
}

static void dump_tags( t_client *client, const char *code, it_tag *it )
{
	char buf[BUFLENTAG];
	t_tag *t;

	for( t = it_tag_begin(it); t; t = it_tag_next(it) ){
		mktag(buf, BUFLENTAG, t) ;
		RLINE(code,"%s", buf ); 
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

	mktag(buf, BUFLENTAG, t);
	RLAST("271", "%s", buf ); 
	tag_free(t);
}

CMD(cmd_tag2id, r_guest, p_idle, arg_need )
{
	int t;

	if( 0 > (t = tag_id( line ))){
		RLAST("511", "no such tag" );
		return;
	}
	RLAST("272", "%d", t);
}

CMD(cmd_tagadd, r_admin, p_idle, arg_need )
{
	int id;

	if( 0 > (id = tag_add( line ))){
		RLAST("511", "failed" );
		return;
	}

	RLAST( "273", "%d", id );
}

CMD(cmd_tagsetname, r_admin, p_idle, arg_need )
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

CMD(cmd_tagsetdesc, r_admin, p_idle, arg_need )
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

CMD(cmd_tagdel, r_admin, p_idle, arg_need )
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

CMD(cmd_tracktaglist, r_guest, p_idle, arg_need )
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

CMD(cmd_tracktagset, r_admin, p_idle, arg_need )
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

CMD(cmd_tracktagdel, r_admin, p_idle, arg_need )
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
 * commands: album
 */

CMD(cmd_albumget, r_guest, p_idle, arg_need )
{
	int id;
	char *end;
	t_album *a;
	char buf[BUFLENALBUM];

	id = strtol(line, &end, 10 );
	if( *end ){
		RBADARG( "invalid album ID");
		return;
	}

	if( NULL == (a = album_get(id))){
		RLAST( "580", "failed" );
		return;
	}

	mkalbum(buf, BUFLENALBUM, a);
	RLAST( "280", "%s\n", buf ); 
	album_free(a);
}


static void dump_albums( t_client *client, const char *code, it_album *it )
{
	char buf[BUFLENALBUM];
	t_album *t;

	for( t = it_album_begin(it); t; t = it_album_next(it) ){
		mkalbum(buf, BUFLENALBUM, t) ;
		RLINE(code,"%s", buf ); 
		album_free(t);
	}

	RLAST(code, "" );
}

CMD(cmd_albumlist, r_guest, p_idle, arg_none )
{
	it_album *it;

	(void)line;
	it = albums_list( );
	dump_albums( client, "281", it );
	it_album_done(it);
}

CMD(cmd_albumsartist, r_admin, p_idle, arg_need )
{
	char *end;
	int id;
	it_album *it;

	id = strtol(line, &end, 10);
	if( *end ){
		RBADARG( "expecting only an artist ID");
		return;
	}

	it = albums_artistid(id);
	dump_albums( client, "285", it );
	it_album_done(it);
}

CMD(cmd_albumsearch, r_guest, p_idle, arg_need )
{
	it_album *it;

	it = albums_search( line );
	dump_albums( client, "282", it );
	it_album_done(it);
}

// TODO: cmd_albumsearchf

CMD(cmd_albumsetname, r_admin, p_idle, arg_need )
{
	char *end;
	int id;
	t_album *a;

	id = strtol( line, &end, 10 );
	if( line == end ){
		RBADARG( "ecpecting an album ID" );
		return;
	}

	if( NULL == (a = album_get(id))){
		RLAST( "512", "no such album" );
		return;
	}

	end += strspn(end, " \t" );
	if( album_setname(a, end )){
		RLAST("511", "failed" );
		album_free(a);
		return;
	}

	album_save(a);
	album_free(a);
	RLAST("283", "name changed" );
}

CMD(cmd_albumsetartist, r_admin, p_idle, arg_need )
{
	char *s, *e;
	int album;
	int artist;
	t_album *a;

	album = strtol(line, &e, 10 );
	if( line == e ){
		RBADARG( "missing/invalid album id");
		return;
	}

	s = e + strspn(e, "\t " );
	artist = strtol(s, &e, 10 );
	if( *e ){
		RBADARG( "missing/invalid artist id");
		return;
	}

	if( NULL == (a = album_get(album))){
		RLAST( "512", "no such album" );
		return;
	}

	if( album_setartist(a, artist)){
		album_free(a);
		RLAST("511", "failed" );
		return;
	}

	album_save(a);
	album_free(a);
	RLAST("284", "artist changed" );
}



/************************************************************
 * commands: artist
 */

CMD(cmd_artistget, r_guest, p_idle, arg_need )
{
	int id;
	char *end;
	t_artist *a;
	char buf[BUFLENALBUM];

	id = strtol(line, &end, 10 );
	if( *end ){
		RBADARG( "invalid artist ID");
		return;
	}

	if( NULL == (a = artist_get(id))){
		RLAST( "590", "failed" );
		return;
	}

	mkartist(buf, BUFLENALBUM, a);
	RLAST( "290", "%s\n", buf ); 
	artist_free(a);
}


static void dump_artist( t_client *client, const char *code, it_artist *it )
{
	char buf[BUFLENALBUM];
	t_artist *t;

	for( t = it_artist_begin(it); t; t = it_artist_next(it) ){
		mkartist(buf, BUFLENALBUM, t) ;
		RLINE(code,"%s", buf ); 
		artist_free(t);
	}

	RLAST(code, "" );
}

CMD(cmd_artistlist, r_guest, p_idle, arg_none )
{
	it_artist *it;

	(void)line;
	it = artist_list( );
	dump_artist( client, "291", it );
	it_artist_done(it);
}

CMD(cmd_artistsearch, r_guest, p_idle, arg_need )
{
	it_artist *it;

	it = artist_search( line );
	dump_artist( client, "292", it );
	it_artist_done(it);
}

CMD(cmd_artistsetname, r_admin, p_idle, arg_need )
{
	char *end;
	int id;
	t_artist *a;

	id = strtol( line, &end, 10 );
	if( line == end ){
		RBADARG( "ecpecting an artist ID" );
		return;
	}

	if( NULL == (a = artist_get(id))){
		RLAST( "512", "no such artist" );
		return;
	}

	end += strspn(end, " \t" );
	if( artist_setname(a, end )){
		RLAST("511", "failed" );
		artist_free(a);
		return;
	}

	artist_save(a);
	artist_free(a);
	RLAST("293", "name changed" );
}

CMD(cmd_artistmerge, r_admin, p_idle, arg_need )
{
	char *s, *e;
	int fromid;
	int toid;

	fromid = strtol(line, &e, 10 );
	if( line == e ){
		RBADARG( "bad from_artist_id");
		return;
	}

	s = e + strspn(e, "\t " );
	toid = strtol(s, &e, 10 );
	if( *e ){
		RBADARG( "bad to_artist_id");
		return;
	}

	if( artist_merge(fromid, toid )){
		RLAST("511", "failed" );
		return;
	}

	RLAST("294", "merged artist %d into %d", fromid, toid );
}

CMD(cmd_artistadd, r_admin, p_idle, arg_need )
{
	RLAST( "555", "TODO: artistadd");
	(void)line;
}

CMD(cmd_artistdel, r_admin, p_idle, arg_need )
{
	RLAST( "555", "TODO: artisdel");
	(void)line;
}

/************************************************************
 * commands: sfilter
 */

static void dump_sfilters( t_client *client, const char *code, it_sfilter *it )
{
	char buf[BUFLENTAG];
	t_sfilter *t;

	for( t = it_sfilter_begin(it); t; t = it_sfilter_next(it) ){
		mksfilter(buf, BUFLENTAG, t) ;
		RLINE(code,"%s", buf ); 
		sfilter_free(t);
	}

	RLAST(code, "" );
}

CMD(cmd_sfilterlist, r_guest, p_idle, arg_none )
{
	it_sfilter *it;

	(void)line;
	it = sfilters_list();
	dump_sfilters( client, "270", it );
	it_sfilter_done(it );
}

CMD(cmd_sfilterget, r_guest, p_idle, arg_need )
{
	char buf[BUFLENTAG];
	t_sfilter *t;
	char *end;
	int id;

	id = strtol( line, &end, 10 );
	if( *end ){
		RBADARG( "ecpecting a sfilter ID" );
		return;
	}

	if( NULL == (t = sfilter_get( id ))){
		RLAST("511", "no such sfilter" );
		return;
	}

	mksfilter(buf, BUFLENTAG, t);
	RLAST("254", "%s", buf ); 
	sfilter_free(t);
}

CMD(cmd_sfilter2id, r_guest, p_idle, arg_need )
{
	int t;

	if( 0 > (t = sfilter_id( line ))){
		RLAST("511", "no such sfilter" );
		return;
	}
	RLAST("255", "%d", t);
}

CMD(cmd_sfilteradd, r_admin, p_idle, arg_need )
{
	int id;

	if( 0 > (id = sfilter_add( line ))){
		RLAST("511", "failed" );
		return;
	}

	RLAST( "256", "%d", id );
}

CMD(cmd_sfiltersetname, r_admin, p_idle, arg_need )
{
	char *end;
	int id;

	id = strtol( line, &end, 10 );
	if( line == end ){
		RBADARG( "ecpecting a sfilter ID" );
		return;
	}

	end += strspn(end, " \t" );
	if( sfilter_setname(id, end )){
		RLAST("511", "failed" );
		return;
	}

	RLAST("257", "name changed" );
}

CMD(cmd_sfiltersetfilter, r_admin, p_idle, arg_need )
{
	char *end;
	int id;
	expr *e = NULL;
	char *msg;
	int pos;

	id = strtol( line, &end, 10 );
	if( line == end ){
		RBADARG( "ecpecting a sfilter ID" );
		return;
	}

	end += strspn(end, " \t" );
	if( NULL == (e = expr_parse_str( &pos, &msg, end ))){
		RLAST( "511", "error at pos %d in filter: %s", pos, msg );
		return;
	}

	if( sfilter_setfilter(id, end )){
		expr_free(e);
		RLAST("511", "failed" );
		return;
	}

	expr_free(e);
	RLAST("258", "filter changed" );
}

CMD(cmd_sfilterdel, r_admin, p_idle, arg_need )
{
	char *end;
	int id;

	id = strtol( line, &end, 10 );
	if( *end ){
		RBADARG( "ecpecting a sfilter ID" );
		return;
	}

	if( sfilter_del( id )){
		RLAST("511", "failed" );
		return;
	}

	RLAST("259", "deleted" );
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
		
		if( ( client->user ? client->user->right : r_any ) < c->right )
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

		if( c->right > (client->user ? client->user->right : r_any)){
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
 * process each incoming line
 */
static void proto_input( t_client *client )
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

/*
 * initialize protocol for a newly connected client
 */
static void proto_newclient( t_client *client )
{
	// TODO: make greeting versioned
	client->ifunc = (void*)proto_input;
	RLAST( "220", "dudld" );
	syslog( LOG_DEBUG, "con #%d: new connection from %s", client->id,
			inet_ntoa(client->sin.sin_addr ));
}

/*
 * cleanup proto when a client disconnected
 */
static void proto_delclient( t_client *client )
{
	syslog( LOG_DEBUG, "con #%d: lost connection to %s", client->id,
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
	player_func_elapsed = proto_bcast_player_elapsed;

	sleep_func_set = proto_bcast_sleep;

	random_func_filter = proto_bcast_filter;

	queue_func_add = proto_bcast_queue_add;
	queue_func_del = proto_bcast_queue_del;
	queue_func_clear = proto_bcast_queue_clear;
	queue_func_fetch = proto_bcast_queue_fetch;

	tag_func_changed = proto_bcast_tag_changed;
	tag_func_del = proto_bcast_tag_del;
}



