#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "proto_helper.h"


#define BUFLENLINE	4096

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
int proto_rline( t_client *client, const char *code, 
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
int proto_rlast( t_client *client, const char *code, 
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
 * broadcast a reply to all clients with at least "right" rights.
 */
void proto_bcast( t_rights right, const char *code, 
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

void proto_player_reply( t_client *client, t_playstatus r, char *code, char *reply )
{
	switch(r){ 
		case PE_OK: 
			proto_rlast(client, code, "%s", reply );
			break; 

		case PE_NOTHING:
			proto_rlast(client,"541", "nothing to do" );
			return;

		case PE_BUSY:
			proto_rlast(client,"541", "already doing this" );
			return;

		case PE_NOSUP:
			proto_rlast(client,"541", "not supported" );
			return;

		case PE_SYS:
			proto_rlast(client,"540", "player error: %s", 
					strerror(errno));
			return;

		default:
			proto_rlast(client, "541", "player error" );
			return;
	}
}


