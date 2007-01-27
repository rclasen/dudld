#ifndef _COMMONDB_DUDLDB_H
#define _COMMONDB_DUDLDB_H

typedef void (*db_opened_cb)( void );

int db_init( db_opened_cb );
void db_done( void );

typedef void t_db;
typedef void it_db;

t_db *it_db_begin( it_db *i );
t_db *it_db_cur( it_db *i );
t_db *it_db_next( it_db *i );
void it_db_done( it_db *i );

#endif
