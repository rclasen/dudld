#ifndef _USER_H
#define _USER_H

#include "dudldb.h"
#include "client.h"

typedef struct {
	int id;
	t_rights right;
	char *name;
	int _refs;
	char *_pass;
} t_user;

#define it_user it_db
#define it_user_begin(x)	((t_user*)it_db_begin(x))
#define it_user_cur(x)		((t_user*)it_db_cur(x))
#define it_user_next(x)		((t_user*)it_db_next(x))
#define it_user_done(x)		it_db_done(x)

void user_free( t_user *u );
int user_add( const char *name, t_rights right, const char *pass );
int user_del( int uid );

t_user *user_get( int uid );
int user_id( const char *name );
int user_ok( t_user *u, const char *pass );

it_user *users_list( void );
int user_setright( t_user *u, t_rights right );
int user_setpass( t_user *u, const char *newpass );
int user_save( t_user *u );

#endif
