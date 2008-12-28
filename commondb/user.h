/*
 * Copyright (c) 2008 Rainer Clasen
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */

#ifndef _COMMONDB_USER_H
#define _COMMONDB_USER_H

#include "dudldb.h"
//#include "client.h"

typedef enum {
	r_any,
	r_guest,	/* read only access */
	r_user,		/* player/queue control */
	r_admin,	/* Database modification */
	r_master,	/* everything else */
} t_rights;

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
t_user *user_getn( const char *name );
int user_id( const char *name );
int user_ok( t_user *u, const char *pass );

it_user *users_list( void );
int user_setright( int userid, t_rights right );
int user_setpass( int userid, const char *newpass );

#endif
