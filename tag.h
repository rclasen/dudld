#ifndef _TAG_H
#define _TAG_H

#include <db.h>

typedef struct {
	int id;
	char *name;
	char *desc;
} t_tag;

typedef void (*t_tag_func)( t_tag *t );
extern t_tag_func tag_func_changed;
extern t_tag_func tag_func_del;

#define it_tag it_db
#define it_tag_begin(x)		((t_tag*)it_db_begin(x))
#define it_tag_cur(x)		((t_tag*)it_db_cur(x))
#define it_tag_next(x)		((t_tag*)it_db_next(x))
#define it_tag_done(x)		it_db_done(x)

void tag_free( t_tag *t );

t_tag *tag_get( int id );
t_tag *tag_getname( const char *name );

it_tag *tags_list( void );

int tag_add( const char *name );
int tag_setname( int id, const char *name );
int tag_setdesc( int id, const char *desc );
int tag_del( int id );

it_tag *track_tags( int tid );
int track_tagset( int tid, int id );
int track_tagdel( int tid, int id );

int track_tagged( int tid, int id );

// TODO: tagstat

#endif
