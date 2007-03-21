#ifndef _OPT_H
#define _OPT_H

#include "commondb/track.h"

extern int opt_port;
extern char *opt_pidfile;
extern char *opt_path_tracks;

extern int opt_gap;
extern int opt_random;
extern int opt_cut;
extern t_replaygain opt_rgtype;
extern double opt_rgpreamp;
extern int opt_start;
extern char *opt_sfilter;
extern char *opt_failtag;

extern char *opt_db_host;
extern char *opt_db_port;
extern char *opt_db_name;
extern char *opt_db_user;
extern char *opt_db_pass;

void opt_read( char *fname );

#endif


