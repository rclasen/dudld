#ifndef _PROTO_H
#define _PROTO_H

#include "client.h"

void proto_input( t_client *client );
void proto_newclient( t_client *client );
void proto_delclient( t_client *client );


void proto_bcast_login( int uid );
void proto_bcast_logout( int uid );

#endif
