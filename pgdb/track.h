#ifndef _PGDB_TRACK_H
#define _PGDB_TRACK_H

#include <track.h>
#include <pgdb/db.h>

t_track *track_convert( PGresult *res, int tup );

#endif
