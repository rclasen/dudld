/*
 * Copyright (c) 2008 Rainer Clasen
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */

#include <glib.h>
#include <syslog.h>

#include "opt.h"

int opt_port = -1;
char *opt_pidfile = NULL;
char *opt_path_tracks = NULL;

int opt_gap = -1;
int opt_cut = -1;
double opt_rgpreamp = -1;
t_replaygain opt_rgtype = rg_none;
int opt_random = -1;
int opt_start = -1;
char *opt_sfilter = NULL;
char *opt_failtag = NULL;
char *opt_pipeline = NULL;

char *opt_db_host = NULL;
char *opt_db_port = NULL;
char *opt_db_name = NULL;
char *opt_db_user = NULL;
char *opt_db_pass = NULL;

static void def_string( char **dst, GKeyFile *kf, char *key, char *def )
{
	char *tmp;
	GError *err = NULL;

	*dst = def;
	if( kf && NULL != (tmp = g_key_file_get_string( kf, "dudld", key, &err )))
		*dst = tmp;
}

static void def_integer( int *dst, GKeyFile *kf, char *key, int def )
{
	int tmp;
	GError *err = NULL;

	*dst = def;
	if( ! kf )
		return;

	tmp = g_key_file_get_integer( kf, "dudld", key, &err );
	if( err && err->code == G_KEY_FILE_ERROR_INVALID_VALUE )
		syslog( LOG_ERR, "invalid data for %s: %s", key, err->message );

	if( ! err )
		*dst = tmp;
}

static void def_double( double *dst, GKeyFile *kf, char *key, double def )
{
	double tmp;
	GError *err = NULL;

	*dst = def;
	if( ! kf )
		return;

	tmp = g_key_file_get_double( kf, "dudld", key, &err );
	if( err && err->code == G_KEY_FILE_ERROR_INVALID_VALUE )
		syslog( LOG_ERR, "invalid data for %s: %s", key, err->message );

	if( ! err )
		*dst = tmp;
}

static void def_replaygain( t_replaygain *dst, GKeyFile *kf, char *key, t_replaygain def )
{
	int tmp = rg_none;
	GError *err = NULL;

	*dst = def;
	if( ! kf )
		return;

	tmp = g_key_file_get_integer( kf, "dudld", key, &err );
	if( err && err->code == G_KEY_FILE_ERROR_INVALID_VALUE )
		syslog( LOG_ERR, "invalid data for %s: %s", key, err->message );
	if( err )
		return;

	switch(tmp){
	  case rg_none:
	  case rg_track:
	  case rg_track_peak:
	  case rg_album:
	  case rg_album_peak:
		*dst = (t_replaygain)tmp;
		break;

	  default:
		syslog( LOG_ERR, "invaldid data for %s: unknown replaygain type", key );
		break;
	}
}

void opt_read( char *fname )
{
	GKeyFile *keyfile;
	GError *err = NULL;

	keyfile = g_key_file_new();
	if( ! g_key_file_load_from_file( keyfile, fname, G_KEY_FILE_NONE, &err )){
		syslog(LOG_ERR, "failed to read config %s: %s", fname, err->message );
		g_key_file_free( keyfile );
		keyfile = NULL;
	}

	def_integer( &opt_port, keyfile, "port", 4445 );
	def_string( &opt_pidfile, keyfile, "pidfile", "/var/run/dudld/dudld.pid" );
	def_string( &opt_path_tracks, keyfile, "path_tracks", "/pub/fun/mp3/CD" );

	def_integer( &opt_gap, keyfile, "gap", 0 );
	def_integer( &opt_random, keyfile, "random", 1 );
	def_integer( &opt_cut, keyfile, "cut", 1 );
	def_double( &opt_rgpreamp, keyfile, "rgpreamp", 7 );
	def_integer( &opt_start, keyfile, "start", 0 );
	def_string( &opt_sfilter, keyfile, "sfilter", "init" );
	def_string( &opt_failtag, keyfile, "failtag", "failed" );
	def_string( &opt_pipeline, keyfile, "pipeline", "autoaudiosink" );

	def_replaygain( &opt_rgtype, keyfile, "rgtype", 3 );

	def_string( &opt_db_host, keyfile, "db_host", "" );
	def_string( &opt_db_port, keyfile, "db_port", "" );
	def_string( &opt_db_name, keyfile, "db_name", "dudl" );
	def_string( &opt_db_user, keyfile, "db_user", "dudld" );
	def_string( &opt_db_pass, keyfile, "db_pass", "dudld" );

	if( keyfile )
		g_key_file_free( keyfile );
}
