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

#include "user.h"
#include "proto.h"

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

static void proto_badarg( t_client *client, const char *desc )
{
	proto_rlast( client, "501", desc );
}

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
 * broadcasts
 */

static void proto_bcast_login( t_client *client )
{
	proto_bcast( r_user, "630", "%d\t%d\t%s", client->id, client->uid, 
			inet_ntoa(client->sin.sin_addr) );
}

static void proto_bcast_logout( t_client *client )
{
	proto_bcast( r_user, "631", "%d\t%d\t%s", client->id, client->uid, 
			inet_ntoa(client->sin.sin_addr) );
}

// TODO: more broadcasts


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

		RLINE( "230", "%d\t%d\t%s", c->id, c->uid, 
				inet_ntoa(c->sin.sin_addr));
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

CMD(cmd_play)
{
	IDLE;
	RNOARGS;

	RLAST( "541", "player error" );
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

	{ NULL, NULL, 0 }
};

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

void proto_newclient( t_client *client )
{
	RLAST( "220", "hello" );
	printf( "new client\n");
}

void proto_delclient( t_client *client )
{
	printf( "a client is gone\n" );
	proto_bcast_logout( client );
}

// TODO: greeting
// TODO: auth
