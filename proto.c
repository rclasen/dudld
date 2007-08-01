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
#include <stdio.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <string.h>
#include <ctype.h>

#include <config.h>
#include "player.h"
#include "sleep.h"
#include "proto.h"
#include "proto_cmd.h"
#include "proto_helper.h"
#include "proto_bcast.h"

#define SKIPSPACE(str)	while(isspace(*str)){str++;};

/*
 * major version: increased on incompatible protocol changes
 */
#define PROTO_MAJOR_VERSION 1

/*
 * minor version: increased on non-intrusive protocl additions
 */
#define PROTO_MINOR_VERSION 2

static t_cmd *cmd_find( t_protstate context, char *name )
{
	t_cmd *cmd;

	for( cmd = proto_cmds; cmd && cmd->name; cmd++ ){
		if( cmd->context != context && cmd->context != p_any )
			continue;
		if( 0 == strcmp(cmd->name, name))
			return cmd;
	}

	return NULL;
}


static void cmd_arg_free( t_cmd *cmd, void **argv )
{
	void **ndat;
	t_cmd_arg *narg;

	for( ndat = argv, narg = cmd->args; *ndat; ndat++, narg++ ){
		//assert(narg);

		if( narg->free )
			(*narg->free)(*ndat);
	}
	free(argv);
}

static int cmd_parse( t_client *client, t_cmd *cmd, char *line )
{
	char *next = line;
	char *end;
	t_cmd_arg *arg;
	int argc = 0;
	void **argv = NULL;
	int missing = 0;

	if( NULL == (argv = malloc(sizeof(void*))))
		return -1;
	argv[0] = NULL;

	for( arg = cmd->args; arg && arg->name; arg++ ){
		void *data;
		void **tmp;

		SKIPSPACE(next);
		if( *next == 0 ){
			missing++;
			continue;
		}

		data = (*arg->parse)( next, &end );
		if( end == next || ( *end && !isspace(*end) )){
			proto_rlast( client, "501", 
					"invalid data for argument %s", 
					arg->name );
			goto clean1;
		}
		next = end;

		if( NULL == (tmp = realloc(argv, (argc +2) * sizeof(void*)))){
			proto_rlast( client, "501", "internal error" );
			goto clean1;
		}

		argv = tmp;
		argv[argc++] = (void*)data;
		argv[argc] = NULL;

	}


	if( *next != 0 ){
		proto_rlast( client, "501", "too many arguments" );
		goto clean1;
	}

	if( missing ){
		proto_rlast( client, "501", "missing arguments" );
		goto clean1;
	}

	(*cmd->run)( client, cmd->code, argv );

	cmd_arg_free(cmd, argv);
	return 0;
	
clean1:
	cmd_arg_free(cmd, argv);
	return -1;

}

static void proto_line( t_client *client, char *line )
{
	char *end;
	char *scmd;
	t_cmd *cmd;
	t_rights perm;

	SKIPSPACE(line);
	scmd = val_name( line, &end );
	if( line == end || ! scmd ){
		proto_rlast( client, "501", "invalid command");
		return;
	}
	line = end;

	if( NULL == (cmd = cmd_find( client->pstate, scmd ))){
		proto_rlast( client, "501", "no such command" );
		return;
	}

	perm = client->user ? client->user->right : r_any;
	if( perm < cmd->perm ){
		proto_rlast( client, "501", "insufficient karma" );
		return;
	}

	cmd_parse( client, cmd, line );
}

/*
 * process each incoming line
 */
static void proto_input( t_client *client )
{
	char *line;

	while( NULL != (line = client_getline( client) )){
		proto_line( client, line );
		free(line);
	} 
}

/*
 * initialize protocol for a newly connected client
 */
static void proto_newclient( t_client *client )
{
	client->ifunc = (void*)proto_input;
	proto_rlast( client, "220", "dudld %d %d", 
			PROTO_MAJOR_VERSION, PROTO_MINOR_VERSION );
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



