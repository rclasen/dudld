#ifndef _PGDB_FILTER_H
#define _PGDB_FILTER_H

#include <stdlib.h>

#include <commondb/parseexpr.h>

int sql_expr( char *buf, size_t len, expr *e );

#endif
