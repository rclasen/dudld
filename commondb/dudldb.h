#ifndef _DUDLDB_H
#define _DUDLDB_H

int db_init( void );
void db_done( void );

typedef void t_db;
typedef void it_db;

t_db *it_db_begin( it_db *i );
t_db *it_db_cur( it_db *i );
t_db *it_db_next( it_db *i );
void it_db_done( it_db *i );

#endif
