#ifndef _COMMONDB_DUDLDB_H
#define _COMMONDB_DUDLDB_H

typedef void (*db_opened_cb)( void );

/*
 * open + close DB
 */
int db_init( db_opened_cb );
void db_done( void );

/*
 * t_db: placeholder for row structures
 */
typedef void t_db;

/*
 * it_db: standard DB iterator
 *
 * owns the PQResult and frees it in it_db_done().
 *
 * parses each row and returns it in a newly allocated struct. i.e. it
 * holds no further reference to this struct after creation. The caller
 * has to free the returned row structures he requested.
 *
 */
typedef void it_db;

/* rewind iterator to the first element */
t_db *it_db_begin( it_db *i );
/* parses + allocates current row */
t_db *it_db_cur( it_db *i );
/* proceeds to next row, parses + allocates it */
t_db *it_db_next( it_db *i );
/* frees iterator data + PQresult (no other data is held) */
void it_db_done( it_db *i );

#endif
