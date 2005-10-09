#ifndef _PROTO_BCAST_H
#define _PROTO_BCAST_H

#include "proto_helper.h"

void proto_bcast_login( t_client *client );
void proto_bcast_logout( t_client *client );
void proto_bcast_player_newtrack( void );
void proto_bcast_player_stop( void );
void proto_bcast_player_pause( void );
void proto_bcast_player_resume( void );
void proto_bcast_player_random( void );
void proto_bcast_player_elapsed( void );
void proto_bcast_sleep( void );
void proto_bcast_filter( void );
void proto_bcast_queue_fetch( t_queue *q );
void proto_bcast_queue_add( t_queue *q );
void proto_bcast_queue_del( t_queue *q );
void proto_bcast_queue_clear( void );
void proto_bcast_tag_changed( t_tag *t );
void proto_bcast_tag_del( t_tag *t );

#endif
