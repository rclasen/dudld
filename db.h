#ifndef _DB_H
#define _DB_H

int db_init( void );
void db_done( void );

void *it_db_begin( void *i );
void *it_db_cur( void *i );
void *it_db_next( void *i );
void it_db_done( void *i );

#endif
