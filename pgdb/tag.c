
#include <stdlib.h>
#include <syslog.h>

#include <pgdb/db.h>
#include <pgdb/track.h>
#include <tag.h>

t_tag_func tag_func_changed = NULL;
t_tag_func tag_func_del = NULL;

#define GETFIELD(var,field,gofail) \
	if( -1 == (var = PQfnumber(res, field ))){\
		syslog( LOG_ERR, "missing tag data: %s", field ); \
		goto gofail; \
	}

static t_tag *tag_convert( PGresult *res, int tup )
{
	t_tag *h;
	int f;

	if( ! res )
		return NULL;

	if( tup >= PQntuples(res) )
		return NULL;

	if( NULL == (h = malloc(sizeof(t_tag))))
		return NULL;

	GETFIELD(f,"id", clean1 );
	h->id = pgint(res, tup, f);

	GETFIELD(f,"name", clean1 );
	if( NULL == ( h->name = pgstring(res, tup, f )))
		goto clean1;

	GETFIELD(f,"cmnt", clean1 );
	if( NULL == ( h->desc = pgstring(res, tup, f )))
		goto clean2;

	return h;

clean2:
	free(h->name);

clean1:
	free(h);

	return NULL;
}


void tag_free( t_tag *t )
{
	if( ! t )
		return;

	free(t->name);
	free(t);
}

t_tag *tag_get( int id )
{
	PGresult *res;
	t_tag *t;

	res = db_query( "SELECT id, name, cmnt "
			"FROM mserv_tag WHERE id = %d",id);
	if( ! res || PQresultStatus(res) != PGRES_TUPLES_OK ){
		syslog( LOG_ERR, "tag_get: %s", db_errstr());
		PQclear(res);
		return NULL;
	}

	t = tag_convert(res, 0 );
	PQclear(res);

	return t;
}

int tag_id( const char *name )
{
	PGresult *res;
	int id;
	char *esc;

	if( NULL == (esc = db_escape(name)))
		return -1;

	res = db_query( "SELECT id FROM mserv_tag "
			"WHERE name = '%s'", esc);
	free(esc);
	if( ! res || PQresultStatus(res) != PGRES_TUPLES_OK ){
		syslog( LOG_ERR, "tag_id: %s", db_errstr());
		PQclear(res);
		return -1;
	}

	if( PQntuples(res) != 1 ){
		PQclear(res);
		return -1;
	}

	id = pgint( res, 0, 0);
	PQclear(res);

	return id;
}


it_tag *tags_list( void )
{
	return db_iterate( (db_convert)tag_convert, 
			"SELECT id, name, cmnt "
			"FROM mserv_tag "
			"ORDER BY name" );
}

int tag_add( const char *name )
{
	PGresult *res;
	char *esc;
	int id;

	res = db_query( "SELECT nextval('mserv_tag_id_seq')" );
	if( !res || PQresultStatus(res) != PGRES_TUPLES_OK ){
		syslog( LOG_ERR, "tag_add: %s", db_errstr());
		PQclear(res);
		return -1;
	}

	if( 0 > (id = pgint(res,0,0))){
		PQclear(res);
		return -1;
	}
	PQclear(res);

	if( NULL == (esc = db_escape(name)))
		return -1;

	res = db_query( "INSERT INTO mserv_tag(id, name) "
			"VALUES( %d, '%s')", id, esc );
	free(esc);
	if( ! res || PQresultStatus(res) != PGRES_COMMAND_OK ){
		syslog( LOG_ERR, "tag_add: %s", db_errstr());
		PQclear(res);
		return -1;
	}

	PQclear(res);

	if( tag_func_changed ){
		t_tag *t;

		if( NULL != (t = tag_get(id))){
			(*tag_func_changed)(t);
			tag_free(t);
		}
	}

	return id;
}

int tag_del( int id )
{
	t_tag *t;
	PGresult *res;

	if( tag_func_del )
		t = tag_get(id);

	res = db_query("DELETE FROM mserv_tag WHERE id = %d", id);
	if( ! res || PQresultStatus(res) != PGRES_COMMAND_OK ){
		syslog( LOG_ERR, "tag_del: %s", db_errstr());
		PQclear(res);
		return -1;
	}

	if( tag_func_del  && t ){
		(*tag_func_changed)(t);
		tag_free(t);
	}

	PQclear(res);
	return 0;
}

int tag_setname( int id, const char *name )
{
	PGresult *res;
	char *esc;

	if( NULL == (esc = db_escape( name )))
		return -1;

	res = db_query( "UPDATE mserv_tag SET name ='%s' "
			"WHERE id = %d", esc, id );
	free(esc);

	if( ! res || PQresultStatus(res) != PGRES_COMMAND_OK ){
		syslog( LOG_ERR, "tag_setname: %s", db_errstr());
		PQclear(res);
		return -1;
	}

	PQclear(res);

	if( tag_func_changed ){
		t_tag *t;

		if( NULL != (t = tag_get(id))){
			(*tag_func_changed)(t);
			tag_free(t);
		}
	}

	return 0;
}

int tag_setdesc( int id, const char *desc )
{
	PGresult *res;
	char *esc;

	if( NULL == (esc = db_escape( desc )))
		return -1;

	res = db_query( "UPDATE mserv_tag SET cmnt ='%s' "
			"WHERE id = %d", esc, id );
	free(esc);

	if( ! res || PQresultStatus(res) != PGRES_COMMAND_OK ){
		syslog( LOG_ERR, "tag_setdesc: %s", db_errstr());
		PQclear(res);
		return -1;
	}

	PQclear(res);

	if( tag_func_changed ){
		t_tag *t;

		if( NULL != (t = tag_get(id))){
			(*tag_func_changed)(t);
			tag_free(t);
		}
	}

	return 0;
}

it_tag *track_tags( int tid )
{
	return db_iterate( (db_convert)tag_convert, 
			"SELECT tg.id, tg.name, tg.cmnt "
			"FROM mserv_tag tg "
				"INNER JOIN mserv_filetag tt "
				"ON tg.id = tt.tag_id "
				"INNER JOIN stor_file t "
				"ON tt.file_id = t.id "
			"WHERE t.id = %d "
			"ORDER BY tg.name", tid );
}

int track_tagged( int tid, int id )
{
	PGresult *res;

	res = db_query( "SELECT tag_id FROM mserv_filetag "
			"WHERE file_id = %d AND tag_id = %d",
			tid,id);
	if( ! res || PQresultStatus(res) != PGRES_TUPLES_OK ){
		syslog( LOG_ERR, "track_tagged: %s", db_errstr());
		PQclear(res);
		return -1;
	}

	if( PQntuples(res) > 0 ){
		PQclear(res);
		return 1;
	}
	PQclear(res);
	return 0;
}

int track_tagset( int tid, int id )
{
	PGresult *res;
	int r;

	/* does desired tag exist? */
	res = db_query( "SELECT id FROM mserv_tag WHERE id = %d", id );
	if( ! res || PQresultStatus(res) != PGRES_TUPLES_OK ){
		syslog( LOG_ERR, "track_tagset: %s", db_errstr());
		PQclear(res);
		return -1;
	}

	if( PQntuples(res) == 0 ){
		PQclear(res);
		return -2;
	}
	PQclear(res);


	if( 0 > ( r= track_tagged(tid, id) ))
		return -1;

	if( 0 < r )
		return 0;

	res = db_query( "INSERT INTO mserv_filetag( tag_id, file_id ) "
			"VALUES( %d, %d )", id, tid );
	if( ! res || PQresultStatus(res) != PGRES_COMMAND_OK ){
		syslog( LOG_ERR, "track_tagset: %s", db_errstr());
		PQclear(res);
		return -1;
	}

	PQclear(res);
	return 0;
}

int track_tagdel( int tid, int id )
{
	PGresult *res;

	res = db_query( "DELETE FROM mserv_filetag "
			"WHERE tag_id = %d AND file_id = %d ",
			id, tid );
	if( ! res || PQresultStatus(res) != PGRES_COMMAND_OK ){
		syslog( LOG_ERR, "track_tagdel: %s", db_errstr());
		PQclear(res);
		return -1;
	}

	PQclear(res);
	return 0;
}



