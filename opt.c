#include <glib.h>
#include <syslog.h>

#include "opt.h"

int opt_port = -1;
char *opt_pidfile = NULL;
char *opt_path_tracks = NULL;

int opt_gap = -1;
int opt_random = -1;
int opt_start = -1;
char *opt_sfilter = NULL;
char *opt_failtag = NULL;
/* TODO: make gst/gstreamer pipe configurable: gst_parse_launch()  */

char *opt_db_host = NULL;
char *opt_db_port = NULL;
char *opt_db_name = NULL;
char *opt_db_user = NULL;
char *opt_db_pass = NULL;

static void def_string( char **dst, GKeyFile *kf, char *key, char *def )
{
	GError *err = NULL;

	if( *dst )
		return;

	if( kf )
		*dst = g_key_file_get_string( kf, "dudld", key, &err );

	if( ! *dst )
		*dst = def;
}

static void def_integer( int *dst, GKeyFile *kf, char *key, int def )
{
	GError *err = NULL;

	if( *dst >= 0 )
		return;

	if( kf )
		*dst = g_key_file_get_integer( kf, "dudld", key, &err );
	if( err && err->code == G_KEY_FILE_ERROR_INVALID_VALUE )
		syslog( LOG_ERR, "invalid data for %s: %s", key, err->message );

	if( err || *dst < 0 )
		*dst = def;
}

void opt_read( char *fname )
{
	GKeyFile *keyfile = NULL;
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
	def_integer( &opt_start, keyfile, "start", 0 );
	def_string( &opt_sfilter, keyfile, "sfilter", "init" );
	def_string( &opt_failtag, keyfile, "failtag", "failed" );

	def_string( &opt_db_host, keyfile, "db_host", "" );
	def_string( &opt_db_port, keyfile, "db_port", "" );
	def_string( &opt_db_name, keyfile, "db_name", "dudl" );
	def_string( &opt_db_user, keyfile, "db_user", "dudld" );
	def_string( &opt_db_pass, keyfile, "db_pass", "dudld" );

	if( keyfile )
		g_key_file_free( keyfile );
}
