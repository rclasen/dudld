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
 * 4yz: failure (temporary or non-fatal)
 * 5yz: error (permanent fatal for requested operation)
 *
 * 6yz: broadcast message
 *
 * x0z: syntax
 * x1z: information
 * x2z: Connection
 * x3z: users
 * x4z: player
 * x5z: queue/filter
 * x6z: database
 *
 */

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <errno.h>

#include "user.h"
#include "player.h"
#include "proto.h"

// TODO: escape tabs in output 

typedef enum {
	arg_none,
	arg_opt,
	arg_need,
} t_args;

#define STRERR strerror(errno)

static int proto_vline( t_client *client, int last, const char *code, 
		const char *fmt, va_list ap )
{
	char buffer[10240];
	int r;

	sprintf( buffer, "%3.3s%c", code, last ? ' ' : '-' );

	r = vsnprintf( buffer + 4, 10240 - 5, fmt, ap );
	if( r < 0 || r > 10240 - 5 )
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

/* make a reply string from client data */
static char *mkclient( t_client *c )
{
	static char buffer[100];
	int r;

	r = snprintf( buffer, 100, "%d\t%d\t%s",
			c->id, c->uid, inet_ntoa(c->sin.sin_addr));
	if( r < 0 || r > 100 )
		return NULL;

	return buffer;
}

/* make a reply string from track data */
static char *mktrack( t_track *t )
{
	static char buffer[2048];
	int r;

	r = snprintf( buffer, 2048, "%d\t%d\t%d\t%s\t%d\t%d\t%d", 
			t->id,
			t->albumid,
			t->albumnr,
			t->title,
			t->artistid,
			t->duration,
			t->lastplay
			);
	if( r < 0 || r > 100 )
		return NULL;

	return buffer;
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
	proto_bcast( r_user, "630", "%s", mkclient(client));
}

static void proto_bcast_logout( t_client *client )
{
	proto_bcast( r_user, "631", "%s", mkclient(client));
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
	t_client *c;

	(void)line;
	for( c = clients; c; c = c->next ){
		if( c->close )
			continue;

		RLINE( "230", "%s", mkclient(c));
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
	t_track *track;

	track = player_track();
	proto_bcast( r_guest, "640", "%s", mktrack(track) );
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

// TODO: cmd_status, r_guest }, // playstatus + track
CMD(cmd_status, r_guest, p_idle, arg_none )
{
	(void)line;

	RLAST( "243", "%d", player_status() );
}

// TODO: gap

/************************************************************
 * commands: track 
 */


CMD(cmd_trackget, r_guest, p_idle, arg_need )
{
	char *buf;
	int id;
	t_track *t;

	id = strtol(line, &buf, 10);
	if( *buf ){
		RBADARG( "expecting only a track ID");
		return;
	}

	if( ! (t = track_get( id ))){
		RLAST("510", "no such track" );
		return;
	}

	RLAST( "210", "%s", mktrack(t) );
}

static void dump_tracks( t_client *client, const char *code, it_track *it )
{
	t_track *t;

	for( t = it_track_begin(it); t; t = it_track_next(it) ){
		RLINE(code,"%s", mktrack(t) );
	}
	it_track_done(it);

	RLAST(code, "" );
}

CMD(cmd_tracksearch, r_guest, p_idle, arg_need )
{
	it_track *it;

	it = tracks_search(line);
	dump_tracks( client, "211", it );
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
}

CMD(cmd_trackalter, r_user, p_idle, arg_need )
{
	(void)line;
	// TODO: do something
	RBADARG("blurb");
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
		RLAST( "510", "invalid filter" );
		return;
	}

	RLAST( "251", "filter changed" );
}

CMD(cmd_randomtop, r_guest, p_idle, arg_opt )
{
	it_track *it;
	t_track *t;
	int num;
	char *buf;

	if( *line ){
		num = strtol(line, &buf, 10);
		if( *buf ){
			RBADARG( "expecting only a number");
			return;
		}
	} else {
		num = 20;
	}

	it = random_top(num);
	for( t = it_track_begin(it); t; t = it_track_next(it) ){
		RLINE("252","%s", mktrack(t) );
	}
	it_track_done(it);

	RLAST("252", "" );
}

// TODO: cmd_randomset, r_user },
// TODO: cmd_random, r_user },

/************************************************************
 * command array
 */

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

	random_func_filter = proto_bcast_filter;
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


