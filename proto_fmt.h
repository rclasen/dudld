#ifndef _PROTO_FMT_H
#define _PROTO_FMT_H

#include "proto_helper.h"

char *mkclient( t_client *c );
char *mkuser( t_user *u );
char *mktrack( t_track *t );
char *mkhistory( t_history *h );
char *mktag( t_tag *t );
char *mkalbum( t_album *a );
char *mkartist( t_artist *a );
char *mkqueue( t_queue *q );
char *mksfilter( t_sfilter *t );

void dump_client( t_client *client, const char *code, t_client *t );
void dump_user( t_client *client, const char *code, t_user *t );
void dump_track( t_client *client, const char *code, t_track *t );
//void dump_history( t_client *client, const char *code, t_history *t );
void dump_tag( t_client *client, const char *code, t_tag *t );
void dump_album( t_client *client, const char *code, t_album *t );
void dump_artist( t_client *client, const char *code, t_artist *t );
void dump_queue( t_client *client, const char *code, t_queue *t );
void dump_sfilter( t_client *client, const char *code, t_sfilter *t );

void dump_clients( t_client *client, const char *code, it_client *it );
void dump_users( t_client *client, const char *code, it_user *it );
void dump_tracks( t_client *client, const char *code, it_track *it );
void dump_history( t_client *client, const char *code, it_history *it );
void dump_tags( t_client *client, const char *code, it_tag *it );
void dump_albums( t_client *client, const char *code, it_album *it );
void dump_artists( t_client *client, const char *code, it_artist *it );
void dump_queues( t_client *client, const char *code, it_queue *it );
void dump_sfilters( t_client *client, const char *code, it_sfilter *it );


#endif
