#ifndef _PGDB_ARTIST_H
#define _PGDB_ARTIST_H

#include <commondb/artist.h>
#include "dudldb.h"

t_artist *artist_convert_title( PGresult *res, int tup );
t_artist *artist_convert_album( PGresult *res, int tup );

#endif
