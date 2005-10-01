#ifndef _COMMONDB_SFILTER_H
#define _COMMONDB_SFILTER_H

#include "dudldb.h"

typedef struct _t_sfilter {
	int id;
	char *name;
	char *filter;
} t_sfilter;

#define it_sfilter it_db
#define it_sfilter_begin(x)	((t_sfilter*)it_db_begin(x))
#define it_sfilter_cur(x)	((t_sfilter*)it_db_cur(x))
#define it_sfilter_next(x)	((t_sfilter*)it_db_next(x))
#define it_sfilter_done(x)	it_db_done(x)

void sfilter_free( t_sfilter *t );

t_sfilter *sfilter_get( int id );
int sfilter_id( const char *name );

it_sfilter *sfilters_list( void );

int sfilter_add( const char *name );
int sfilter_setname( int id, const char *name );
int sfilter_setfilter( int id, const char *filter );
int sfilter_del( int id );

#endif
