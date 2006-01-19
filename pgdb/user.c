
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <syslog.h>
#include <crypt.h>

#include <config.h>
#include "dudldb.h"
#include "user.h"

#define GETFIELD(var,field,gofail) \
	if( -1 == (var = PQfnumber(res, field ))){\
		syslog( LOG_ERR, "missing user data: %s", field ); \
		goto gofail; \
	}

static t_user *user_convert( PGresult *res, int tup )
{
	t_user *u;
	int f;

	if( ! res )
		return NULL;

	if( tup >= PQntuples(res) )
		return NULL;

	if( NULL == (u = malloc(sizeof(t_user))))
		return NULL;

	u->_refs = 1;

	GETFIELD(f,"id", clean1 );
	u->id = pgint(res, tup, f);

	GETFIELD(f,"lev", clean1 );
	u->right = pgint(res, tup, f );

	GETFIELD(f,"name", clean1 );
	if( NULL == (u->name = pgstring(res, tup, f)))
		goto clean1;

	GETFIELD(f,"pass", clean1 );
	if( NULL == (u->_pass = pgstring(res, tup, f)))
		goto clean2;

	return u;

clean2:
	free(u->name);
clean1:
	free(u);

	return NULL;
}

void user_free( t_user *u )
{
	if( ! u )
		return;

	if( -- u->_refs )
		return;

	free(u->_pass);
	free(u->name);
	free(u);
}

static char *pass_gen( const char *pass )
{
	char salt[5];

	sprintf( salt, "%02x", (int)( (double)random() / RAND_MAX * 255));
	return crypt(pass, salt);
}

static int pass_ok( const char *cpass, const char *pass )
{
	char *cr;

	if( NULL == (cr = crypt(pass, cpass))){
		return 0;
	}

	return ! strcmp(cpass, cr);
}

int user_add( const char *name, t_rights right, const char *pass )
{
	PGresult *res;
	int uid;

	res = db_query( "SELECT nextval('mserv_user_id_seq')" );
	if( !res || PQresultStatus(res) != PGRES_TUPLES_OK ){
		syslog( LOG_ERR, "user_add: %s", db_errstr());
		PQclear(res);
		return -1;
	}

	if( 0 > (uid = pgint(res,0,0))){
		PQclear(res);
		return -1;
	}
	PQclear(res);

	res = db_query( "INSERT INTO mserv_user( id, name, lev, pass ) "
			"VALUES( %d, '%s', %d, '%s' )", 
			uid, name, right, pass_gen(pass) );
	if( res == NULL || PQresultStatus(res) != PGRES_COMMAND_OK ){
		syslog( LOG_ERR, "user_add: %s", db_errstr() );
		PQclear(res);
		return -1;
	}

	PQclear(res);

	return uid;
}

int user_ok( t_user *u, const char *pass )
{
	if( ! pass_ok(u->_pass, pass )){
		return 0;
	}
		
	return 1;
}

t_user *user_get( int uid )
{
	PGresult *res;
	t_user *u;

	res = db_query( "SELECT * FROM mserv_user WHERE id = %d", uid );
	if( !res || PQresultStatus(res) != PGRES_TUPLES_OK ){
		syslog( LOG_ERR, "user_get: %s", db_errstr());
		PQclear(res);
		return NULL;
	}

	if( PQntuples(res) != 1 ){
		PQclear(res);
		return NULL;
	}

	u = user_convert(res, 0);
	PQclear(res);
	return u;
}

t_user *user_getn( const char *name )
{
	PGresult *res;
	t_user *u;
	char *esc;

	if( NULL == (esc = db_escape(name)))
		return NULL;

	res = db_query( "SELECT * FROM mserv_user WHERE name = '%s'", esc );
	free(esc);
	if( !res || PQresultStatus(res) != PGRES_TUPLES_OK ){
		syslog( LOG_ERR, "user_getn: %s", db_errstr());
		PQclear(res);
		return NULL;
	}

	if( PQntuples(res) != 1 ){
		PQclear(res);
		return NULL;
	}

	u = user_convert(res, 0);
	PQclear(res);
	return u;
}

int user_id( const char *name )
{
	PGresult *res;
	int uid;
	char *esc;

	if( NULL == (esc = db_escape(name)))
		return -1;

	res = db_query( "SELECT id FROM mserv_user WHERE name = '%s'", esc );
	free(esc);
	if( !res || PQresultStatus(res) != PGRES_TUPLES_OK ){
		syslog( LOG_ERR, "user_id: %s", db_errstr());
		PQclear(res);
		return -1;
	}

	if( PQntuples(res) != 1 ){
		PQclear(res);
		return -1;
	}

	uid = pgint(res,0,0);
	PQclear(res);

	return uid;
}

it_user *users_list( void )
{
	return db_iterate( (db_convert)user_convert, "SELECT * "
			"FROM mserv_user ORDER BY LOWER(name)" );
}

int user_setright( int userid, t_rights right )
{
	PGresult *res;

	res = db_query( "UPDATE mserv_user SET lev = %d WHERE id = %d",
			right, userid);
	if( res == NULL || PQresultStatus(res) != PGRES_COMMAND_OK ){
		syslog( LOG_ERR, "user_setright: %s", db_errstr() );
		PQclear(res);
		return -1;
	}

	if( PQcmdTuples(res) == 0 ){
		PQclear(res);
		return -1;
	}

	PQclear(res);

	return 0;
}

int user_setpass( int userid, const char *newpass )
{
	PGresult *res;
	char *pass;

	pass = pass_gen(newpass);

	res = db_query( "UPDATE mserv_user SET pass = '%s' WHERE id = %d",
			pass, userid);
	if( res == NULL || PQresultStatus(res) != PGRES_COMMAND_OK ){
		syslog( LOG_ERR, "user_setpass: %s", db_errstr() );
		PQclear(res);
		return -1;
	}

	if( PQcmdTuples(res) == 0 ){
		PQclear(res);
		return -1;
	}

	PQclear(res);
	return 0;
}

int user_del( int uid )
{
	PGresult *res;

	res = db_query( "DELETE FROM mserv_user WHERE id = %d", uid );
	if( res == NULL || PQresultStatus(res) != PGRES_COMMAND_OK ){
		syslog( LOG_ERR, "user_del: %s", db_errstr() );
		PQclear(res);
		return -1;
	}

	if( PQcmdTuples(res) == 0 ){
		PQclear(res);
		return -1;
	}

	PQclear(res);

	return 0;
}

