#ifndef _PGDB_ALBUM_H
#define _PGDB_ALBUM_H

#include <commondb/album.h>
#include "dudldb.h"

t_album *album_convert( PGresult *res, int tup );

#endif
