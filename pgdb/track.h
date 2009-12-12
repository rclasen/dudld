/*
 * Copyright (c) 2008 Rainer Clasen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */

#ifndef _PGDB_TRACK_H
#define _PGDB_TRACK_H

#include <commondb/track.h>
#include "dudldb.h"

t_track *track_convert( PGresult *res, int tup );

#endif
