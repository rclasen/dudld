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

static void proto_bcast( const char *code, const char *fmt, ... )
{
	t_client *c;
	va_list ap;

	va_start( ap, fmt );
	for( c = clients; c; c = c->next ){
		if( c->close )
			continue;

		proto_vline( c, 1, code, fmt, ap );
	}
	va_end( ap );
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
	

CMD(cmd_quit)
{
	RNOARGS;

	RLAST( "221", "bye" );
	client_close(client);
}

CMD(cmd_play)
{
	RNOARGS;

	RLAST( "541", "player error" );
}

CMD(cmd_who)
{
	t_client *c;

	RNOARGS;
	for( c = clients; c; c = c->next ){
		if( c->close )
			continue;

		RLINE( "230", "%d", 0);
	}
	RLAST( "230", "");
}

	
/************************************************************
 * command array
 */

typedef void (*t_func_cmd)( t_client *client, char *line );

typedef struct _t_cmd {
	const char *name;
	t_func_cmd cmd;
} t_cmd;


static t_cmd proto_cmds[] = {
	{ "QUIT", cmd_quit },
	{ "PLAY", cmd_play },
	{ "WHO", cmd_who },

	{ NULL, NULL}
};

static void cmd( t_client *client, char *line )
{
	t_cmd *c;
	int len;

	len = strcspn(line, " ");
	for( c = proto_cmds; c && c->name; c++ ){
		if( 0 == strncasecmp(c->name, line, len )){
			while( len > 0 && isspace(line[len-1]) )
				line[--len] = 0;

			(*c->cmd)(client, line +len);
			return;
		}
	}

	RLAST( "500", "unkonwn command" );
}


/************************************************************
 * broadcasts
 */

void proto_bcast_login( int uid )
{
	proto_bcast( "630", "%d", uid );
}

void proto_bcast_logout( int uid )
{
	proto_bcast( "631", "%d", uid );
}

// TODO: more broadcasts

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
			
		if( l > 0 )
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
	(void)client;
	printf( "a client is gone\n" );
	proto_bcast_logout( 1 );
}

// TODO: greeting
// TODO: auth
