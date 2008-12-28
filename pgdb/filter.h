/*
 * Copyright (c) 2008 Rainer Clasen
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */

#ifndef _PGDB_FILTER_H
#define _PGDB_FILTER_H

#include <stdlib.h>

#include <commondb/parseexpr.h>

int sql_expr( char *buf, size_t len, expr *e );

#endif
