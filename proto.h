#ifndef _PROTO_H
#define _PROTO_H

#include "client.h"

void proto_init( void );
void proto_input( t_client *client );
void proto_newclient( t_client *client );
void proto_delclient( t_client *client );

#endif
