/*
 * Copyright (c) 2008 Rainer Clasen
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */

#include <stdarg.h>
#include <stdio.h>
#include <syslog.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>

#include "proto_fmt.h"

#define EADDC(len,used,buf,c) if(len > used++ ){ *buf++ = c; }
#define EADD(len,used,buf,c) \
	if( c == '\t' ){\
		EADDC(len, used, buf, '\\' );\
		EADDC(len, used, buf, 't' );\
	} else if( c == '\\' ){\
		EADDC(len, used, buf, '\\' );\
		EADDC(len, used, buf, '\\' );\
	} else {\
		EADDC(len, used, buf, c );\
	}


/* copy string, escaping all \t-s */
static int ecpy( char *buffer, int len, const char *in )
{
	int used = 0;

	buffer[--len] = 0;

	for( ; *in; ++in ){
		EADD(len, used, buffer, *in );

	}
	if( len > used )
		*buffer++ = 0;

	return used;
}

#define BLEN 4096
char buf[BLEN];

/* TODO: get rid of static mkvtab buffer */
static char *mkvtab( const char *fmt, va_list ap )
{
	char *buffer = buf;
	int used = 0;
	int l;

	buf[0] = 0;

	for( ; *fmt; ++fmt ){
		if( used )
			EADDC(BLEN,used,buffer,'\t');

		if( *fmt == 's' ){
			l = ecpy( buffer, BLEN -used, va_arg( ap, char *));
			used += l;
			buffer += l;

		} else if( *fmt == 't' ) {
			char *src = va_arg(ap, char*);

			l = strlen(src);
			if( l < BLEN-used ){
				strcpy( buffer, src );
				used += l;
				buffer +=l;
			}

		} else if( *fmt == 'c' ) {
			int c = va_arg( ap, int );
			EADD(BLEN, used, buffer, c );

		} else if( *fmt == 'd' ) {
			l = snprintf( buffer, BLEN -used, "%d", 
					va_arg(ap, int));
			if( l < 0 )
				l = 0;

			used += l;
			buffer += l;

		} else {
			syslog( LOG_CRIT, "inalid format %c in mkvtab", *fmt );
			EADDC(BLEN,used,buffer,'?');

		}
	}
	if( BLEN > used )
		*buffer++ = 0;

	return strdup(buf);
}

static char *mktab( const char *fmt, ... )
{
	va_list ap;
	char *r;

	va_start( ap, fmt );
	r = mkvtab( fmt, ap );
	va_end( ap );

	return r;
}

char *mkuser( t_user *u )
{
	if( ! u )
		return mktab("dsd", 0, "UNKNOWN", p_any );

	return mktab("dsd", u->id, u->name, u->right );
}

char *mkclient( t_client *c )
{
	char *sub, *tmp;

	if( NULL == (sub = mkuser(c->user)))
		return NULL;

	tmp = mktab( "dst", c->id, inet_ntoa(c->sin.sin_addr), sub );
	free(sub);

	return tmp;
}

char *mktag( t_tag *t )
{
	return mktab("dss", t->id, t->name, t->desc );
}

char *mkartist( t_artist *a )
{
	return mktab("ds", a->id, a->artist );
}

char *mkalbum( t_album *a )
{
	char *sub, *tmp;
	
	if( NULL == (sub = mkartist( a->artist )))
		return NULL;

	tmp = mktab( "dsdt", a->id, a->album, a->year, sub );
	free(sub);

	return tmp;
}

char *mktrack( t_track *t )
{
	char *sub1, *sub2, *tmp;
	
	if( NULL == (sub1 = mkartist( t->artist )))
		return NULL;

	if( NULL == (sub2 = mkalbum( t->album ))){
		free(sub1);
		return NULL;
	}

	/* TODO: nanosec duration, start/stop, replaygain */
	tmp = mktab( "ddsdtt", t->id, t->albumnr, t->title, t->duration,
			sub1, sub2);
	free(sub1);
	free(sub2);
	return tmp;
}

char *mkhistory( t_history *h )
{
	char *sub1, *sub2, *tmp;

	if( NULL == (sub1 = mkuser( h->user )))
		return NULL;

	if( NULL == (sub2 = mktrack( h->track ))){
		free(sub1);
		return NULL;
	}

	tmp = mktab( "dtt", h->played, sub1, sub2);
	free(sub1);
	free(sub2);
	return tmp;
}

char *mkqueue( t_queue *q )
{
	char *sub1, *sub2, *tmp;

	if( NULL == (sub1 = mkuser( q->user )))
		return NULL;

	if( NULL == (sub2 = mktrack( q->track ))){
		free(sub1);
		return NULL;
	}

	tmp = mktab("ddtt", q->id, q->queued, sub1, sub2);
	free(sub1);
	free(sub2);
	return tmp;
}

char *mksfilter( t_sfilter *t )
{
	return mktab("dss", t->id, t->name, t->filter );
}



void dump_client( t_client *client, const char *code, t_client *t)
{
	char *fmt;

	if( NULL == (fmt = mkclient(t))){
		proto_rlast(client, "501", "failed to format" );
		return;
	} 

	proto_rlast(client, code, "%s", fmt );
	free(fmt);
}

void dump_user( t_client *client, const char *code, t_user *t)
{
	char *fmt;

	if( NULL == (fmt = mkuser(t))){
		proto_rlast(client, "501", "failed to format" );
		return;
	} 

	proto_rlast(client, code, "%s", fmt );
	free(fmt);
}

void dump_track( t_client *client, const char *code, t_track *t)
{
	char *fmt;

	if( NULL == (fmt = mktrack(t))){
		proto_rlast(client, "501", "failed to format" );
		return;
	} 

	proto_rlast(client, code, "%s", fmt );
	free(fmt);
}

void dump_tag( t_client *client, const char *code, t_tag *t)
{
	char *fmt;

	if( NULL == (fmt = mktag(t))){
		proto_rlast(client, "501", "failed to format" );
		return;
	} 

	proto_rlast(client, code, "%s", fmt );
	free(fmt);
}

void dump_album( t_client *client, const char *code, t_album *t)
{
	char *fmt;

	if( NULL == (fmt = mkalbum(t))){
		proto_rlast(client, "501", "failed to format" );
		return;
	} 

	proto_rlast(client, code, "%s", fmt );
	free(fmt);
}

void dump_artist( t_client *client, const char *code, t_artist *t)
{
	char *fmt;

	if( NULL == (fmt = mkartist(t))){
		proto_rlast(client, "501", "failed to format" );
		return;
	} 

	proto_rlast(client, code, "%s", fmt );
	free(fmt);
}

void dump_queue( t_client *client, const char *code, t_queue *t)
{
	char *fmt;

	if( NULL == (fmt = mkqueue(t))){
		proto_rlast(client, "501", "failed to format" );
		return;
	} 

	proto_rlast(client, code, "%s", fmt );
	free(fmt);
}

void dump_sfilter( t_client *client, const char *code, t_sfilter *t)
{
	char *fmt;

	if( NULL == (fmt = mksfilter(t))){
		proto_rlast(client, "501", "failed to format" );
		return;
	} 

	proto_rlast(client, code, "%s", fmt );
	free(fmt);
}




void dump_users( t_client *client, const char *code, it_user *it )
{
	char *buf;
	t_user *t;
	int r = 0;

	for( t = it_user_begin(it); t; t = it_user_next(it) ){
		if( r >= 0 && NULL != (buf = mkuser(t))){
			r = proto_rline(client, code,"%s", buf ); 
			free(buf);
		}
		user_free(t);
		if( r < 0 )
			break;
	}

	if( r >= 0 )
		proto_rlast(client, code, "" );
}

void dump_clients( t_client *client, const char *code, it_client *it )
{
	char *buf;
	t_client *t;
	int r = 0;

	for( t = it_client_begin(it); t; t = it_client_next(it) ){
		if( r >= 0 && NULL != (buf = mkclient(t))){
			r = proto_rline(client, code,"%s", buf ); 
			free(buf);
		}
		client_delref(t);
		if( r < 0 )
			break;
	}

	if( r >= 0 )
		proto_rlast(client, code, "" );
}

void dump_tracks( t_client *client, const char *code, it_track *it )
{
	char *buf;
	t_track *t;
	int r = 0;

	for( t = it_track_begin(it); t; t = it_track_next(it) ){
		if( r >= 0 && NULL != (buf = mktrack(t))){
			r = proto_rline(client, code,"%s", buf ); 
			free(buf);
		}
		track_free(t);
		if( r < 0 )
			break;
	}

	if( r >= 0 )
		proto_rlast(client, code, "" );
}

void dump_history( t_client *client, const char *code, it_history *it )
{
	char *buf;
	t_history *t;
	int r = 0;

	for( t = it_history_begin(it); t; t = it_history_next(it) ){
		if( r >= 0 && NULL != (buf = mkhistory(t))){
			r = proto_rline(client, code,"%s", buf ); 
			free(buf);
		}
		history_free(t);
		if( r < 0 )
			break;
	}

	if( r >= 0 )
		proto_rlast(client, code, "" );
}

void dump_tags( t_client *client, const char *code, it_tag *it )
{
	char *buf;
	t_tag *t;
	int r = 0;

	for( t = it_tag_begin(it); t; t = it_tag_next(it) ){
		if( r >= 0 && NULL != (buf = mktag(t) )){
			r = proto_rline(client, code,"%s", buf ); 
			free(buf);
		}
		tag_free(t);
		if( r < 0 )
			break;
	}

	if( r >= 0 )
		proto_rlast(client, code, "" );
}

void dump_albums( t_client *client, const char *code, it_album *it )
{
	char *buf;
	t_album *t;
	int r = 0;

	for( t = it_album_begin(it); t; t = it_album_next(it) ){
		if( r >= 0 && NULL != (buf = mkalbum(t) )){
			r = proto_rline(client, code,"%s", buf ); 
			free(buf);
		}
		album_free(t);
		if( r < 0 )
			break;
	}

	if( r >= 0 )
		proto_rlast(client, code, "" );
}

void dump_artists( t_client *client, const char *code, it_artist *it )
{
	char *buf;
	t_artist *t;
	int r = 0;

	for( t = it_artist_begin(it); t; t = it_artist_next(it) ){
		if( r >= 0 && NULL != (buf = mkartist(t) )){
			r = proto_rline(client, code,"%s", buf ); 
			free(buf);
		}
		artist_free(t);
		if( r < 0 )
			break;
	}

	if( r >= 0 )
		proto_rlast(client, code, "" );
}

void dump_queues( t_client *client, const char *code, it_queue *it )
{
	char *buf;
	t_queue *t;
	int r = 0;

	for( t = it_queue_begin(it); t; t = it_queue_next(it) ){
		if( r >= 0 && NULL != (buf = mkqueue(t) )){
			r = proto_rline(client, code,"%s", buf ); 
			free(buf);
		}
		queue_free(t);
		if( r < 0 )
			break;
	}

	if( r >= 0 )
		proto_rlast(client, code, "" );
}

void dump_sfilters( t_client *client, const char *code, it_sfilter *it )
{
	char *buf;
	t_sfilter *t;
	int r = 0;

	for( t = it_sfilter_begin(it); t; t = it_sfilter_next(it) ){
		if( r >= 0 && NULL != (buf = mksfilter(t) )){
			r = proto_rline(client, code,"%s", buf ); 
			free(buf);
		}
		sfilter_free(t);
		if( r < 0 )
			break;
	}

	if( r >= 0 )
		proto_rlast(client, code, "" );
}


