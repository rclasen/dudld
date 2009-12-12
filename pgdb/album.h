/*
 * Copyright (c) 2008 Rainer Clasen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */

#ifndef _PGDB_ALBUM_H
#define _PGDB_ALBUM_H

#include <commondb/album.h>
#include "dudldb.h"

t_album *album_convert( PGresult *res, int tup );

#endif
