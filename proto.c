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
 * x5z: queue
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
	static char buffer[10];
	int r;

	r = snprintf( buffer, 10, "%d", t->id );
	if( r < 0  || r > 10 )
		return NULL;

	return buffer;
}




/************************************************************
 * commands
 */

#define CMD(name)	static void name( t_client *client, char *line )
#define RLINE(code,text...)	proto_rline( client, code,text )
#define RLAST(code,text...)	proto_rlast( client, code,text )
#define RBADARG(desc)		proto_badarg( client, desc )
#define RNOARGS			\
	if( line && strlen(line) ){\
		proto_badarg( client, "no arguments allowed" ); \
		return; \
	}
#define RARGS			\
	if( !line || !strlen(line) ){\
		proto_badarg( client, "missing arguments" ); \
		return; \
	}
#define STATE(st)	(client->pstate == (st))	
#define IDLE	\
	if( !STATE(p_idle) ){\
		RLAST("520", "waiting for other input" ); \
		return; \
	}

/************************************************************
 * commands: connection
 */

CMD(cmd_quit)
{
	RNOARGS;

	RLAST( "221", "bye" );
	client_close(client);
}

CMD(cmd_disconnect)
{
	int id;
	char *end;
	t_client *c;

	RARGS;
	IDLE;

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

CMD(cmd_user)
{
	if( ! (STATE(p_open) || STATE(p_idle)) ){
		RLAST( "520", "already seen a USER command" );
		return;
	}

	RARGS;

	client->pdata = strdup( line );
	client->pstate = p_user;

	RLAST( "320", "user ok, use PASS for password" );
}

CMD(cmd_pass)
{
	if( ! STATE(p_user) ){
		RLAST( "520", "first issue a USER command" );
		return;
	}

	RARGS;

	if( ! user_ok( client->pdata, line )){
		client->pstate = p_open;
		client->right = r_any;
		client->uid = 0;
		RLAST("52x", "login failed" );
	
	} else {
		client->uid = 1;
		client->pstate = p_idle;
		client->right = r_master;

		RLAST( "221", "successfully logged in" );
		proto_bcast_login(client);
	}

	free( client->pdata );
	client->pdata = NULL;

}

CMD(cmd_who)
{
	t_client *c;

	IDLE;
	RNOARGS;
	for( c = clients; c; c = c->next ){
		if( c->close )
			continue;

		RLINE( "230", "%s", mkclient(client));
	}
	RLAST( "230", "");
}

CMD(cmd_kick)
{
	int uid;
	char *end;
	t_client *c;
	int found = 0;

	IDLE;
	RARGS;

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
#define RPMISC(x)	reply_player(client,x)


CMD(cmd_play)
{
	int r;

	IDLE;
	RNOARGS;

	r = player_start();

	if( r == PE_OK ){
		RLAST( "240", "playing" );
		return;
	}

	RPMISC(r);
}

CMD(cmd_stop)
{
	int r;

	IDLE;
	RNOARGS;

	r = player_stop();

	if( r == PE_OK ){
		RLAST( "241", "stopped" );
		return;
	}

	RPMISC(r);
}

CMD(cmd_next)
{
	int r;

	IDLE;
	RNOARGS;

	r = player_next();

	if( r == PE_OK  ){
		RLAST( "240", "playing" );
		return;
	}

	RPMISC(r);
}

CMD(cmd_prev)
{
	int r;

	IDLE;
	RNOARGS;

	r = player_prev();

	if( r == PE_OK ){
		RLAST( "240", "playing" );
		return;
	}

	RPMISC(r);
}

CMD(cmd_pause)
{
	int r;

	IDLE;
	RNOARGS;

	r = player_pause();

	if( r == PE_OK ){
		RLAST( "242", "paused" );
		return;
	}

	RPMISC(r);
}



/************************************************************
 * command array
 */

typedef void (*t_func_cmd)( t_client *client, char *line );

typedef struct _t_cmd {
	const char *name;
	t_func_cmd cmd;
	t_rights right;
} t_cmd;

static t_cmd proto_cmds[] = {
	{ "QUIT", cmd_quit, r_any },
	{ "DISCONNECT", cmd_disconnect, r_master },

	{ "USER", cmd_user, r_any },
	{ "PASS", cmd_pass, r_any },
	//{ "USERS", cmd_users, r_user },
	//{ "USERGETID", cmd_usergetid, r_user },
	{ "WHO", cmd_who, r_user },
	{ "KICK", cmd_kick, r_master },

	{ "PLAY", cmd_play, r_user },
	{ "STOP", cmd_stop, r_user },
	{ "NEXT", cmd_next, r_user },
	{ "PREV", cmd_prev, r_user },
	{ "PAUSE", cmd_pause, r_user },

	{ NULL, NULL, 0 }
};

/*
 * find apropriate command entry
 * check permissions
 * strip command from line leaving only args
 * and pass args to apropriate function
 */
static void cmd( t_client *client, char *line )
{
	t_cmd *c;
	int len;

	for( c = proto_cmds; c && c->name; c++ ){
		len = strlen(c->name);
		if( 0 == strncasecmp(c->name, line,len )){
			char *s = line + len;

			while( *s && isspace(*s) )
				s++;

			if( c->right <= client->right ){
				(*c->cmd)(client, s);
			} else {
				RLAST( "520", "permission denied");
			}
			return;
		}
	}

	RLAST( "500", "unkonwn command" );
}


/************************************************************
 * interface functions
 */

/*
 * initialize protocol
 */

void proto_init( void )
{
	player_func_newtrack = proto_bcast_player_newtrack;
	player_func_pause = proto_bcast_player_pause;
	player_func_resume = proto_bcast_player_resume;
	player_func_stop = proto_bcast_player_stop;
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

/*
 * initialize protocol for a newly connected client
 */
void proto_newclient( t_client *client )
{
	RLAST( "220", "hello" );
	syslog( LOG_DEBUG, "new connection %d from %s", client->id,
			inet_ntoa(client->sin.sin_addr ));
}

/*
 * cleanup proto when a client disconnected
 */
void proto_delclient( t_client *client )
{
	syslog( LOG_DEBUG, "lost connection %d to %s", client->id,
			inet_ntoa(client->sin.sin_addr ));
	proto_bcast_logout( client );
}

