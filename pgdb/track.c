#define _GNU_SOURCE 1
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <syslog.h>

#include <pgdb/db.h>
#include <pgdb/track.h>




t_random_func random_func_filter = NULL;




// TODO: use something real for filter
static char *filter = NULL;


#define RANDOM_LIMIT 1000

#define TRACK_TAB "mus_title"
#define CACHE_TAB "mserv_cache"

#define TRACK_COMMON	" "\
	"FROM "\
		"( "\
			"mus_title t  "\
				"INNER JOIN stor_file f  "\
				"ON t.id = f.titleid "\
			")  "\
			"INNER JOIN stor_unit u  "\
			"ON f.unitid = u.id " \
	"WHERE "\
		"f.titleid NOTNULL "\
		"AND NOT f.broken "

#define TRACK_QUERY \
	"SELECT "\
		"t.id,"\
		"t.album_id,"\
		"t.nr,"\
		"date_part('epoch',f.duration) AS dur,"\
		"date_part('epoch',t.lastplay) AS lplay,"\
		"t.title,"\
		"t.artist_id,"\
		"stor_filename(u.collection,u.colnum,f.dir,f.fname) "\
			"AS filename " \
	TRACK_COMMON 

#define TRACK_QCACHE \
	"SELECT "\
		"t.id,"\
		"f.duration,"\
		"stor_filename(u.collection,u.colnum,f.dir,f.fname) "\
			"AS filename "\
	TRACK_COMMON

#define CACHE_CREATE "CREATE TEMP TABLE " CACHE_TAB " ("\
	"id INTEGER,"\
	"duration TIME,"\
	"filename VARCHAR"\
	")"

#define CACHE_QUERY \
	"SELECT "\
		"t.id,"\
		"t.album_id," \
		"t.nr,"\
		"date_part('epoch',c.duration) AS dur,"\
		"date_part('epoch',t.lastplay) AS lplay,"\
		"t.title,"\
		"t.artist_id,"\
		"c.filename "\
	"FROM "\
		"mus_title t "\
			"INNER JOIN mserv_cache c "\
			"USING(id) "





#define GETFIELD(var,field,gofail) \
	if( -1 == (var = PQfnumber(res, field ))){\
		syslog( LOG_ERR, "missing track data: %s", field ); \
		goto gofail; \
	}

t_track *track_convert( PGresult *res, int tup )
{
	t_track *t;
	int f;

	if( ! res )
		return NULL;

	/* this is checked by PQgetvalue, too. Checking this in advance
	 * makes the error handling easier. */
	if( tup >= PQntuples(res) )
		return NULL;

	if( NULL == (t = malloc(sizeof(t_track))))
		return NULL;

	t->_refs = 1;
	t->modified.any = 0;

	GETFIELD(f,"id", clean1 );
	t->id = pgint(res, tup, f );

	GETFIELD(f,"album_id", clean1 );
	t->albumid = pgint(res, tup, f);

	GETFIELD(f,"nr", clean1 );
	t->albumnr = pgint(res, tup, f);

	GETFIELD(f,"artist_id", clean1 );
	t->artistid = pgint(res, tup, f);

	t->lastplay = 0;
	if( -1 != (f = PQfnumber( res, "lplay" )))
		t->lastplay = pgint(res, tup, f);

	t->duration = 0;
	if( -1 != (f = PQfnumber( res, "dur" )))
		t->duration = pgint(res, tup, f);

	GETFIELD(f,"filename", clean1 );
	if( NULL == (t->fname = pgstring(res, tup, f)))
		goto clean1;

	GETFIELD(f,"title", clean2 );
	if( NULL == (t->title = pgstring(res, tup, f)))
		goto clean2;

	return t;

clean2:
	free(t->fname);

clean1:
	free(t);

	return NULL;
}

t_track *track_use( t_track *t )
{
	t->_refs ++;
	return t;
}

void track_free( t_track *t )
{
	if( 0 < -- t->_refs)
		return;

	free( t->fname );
	free( t->title );
	free( t );
}


int track_settitle( t_track *t, const char *title )
{
	char *n;

	if( NULL == (n = strdup(title)))
		return 1;

	free(t->title);
	t->title = n;
	t->modified.m.title = 1;
	return 0;
}

int track_setartist( t_track *t, int artistid )
{
	t->artistid = artistid;
	t->modified.m.artistid = 1;
	return 0;
}

static int addcom( char *buffer, int len, int *fields )
{
	if( *fields && len ){
		strcat( buffer, ",");
		(*fields)++;
		return 1;
	}

	return 0;
}

#define SBUFLEN 1024
int track_save( t_track *t )
{
	char buffer[SBUFLEN];
	int len;
	int fields = 0;
	PGresult *res;

	if( ! t->id )
		return 1;

	if( ! t->modified.any )
		return 0;

	len = snprintf( buffer, SBUFLEN, "UPDATE " TRACK_TAB " SET ");
	if( len > SBUFLEN || len < 0 )
		return 1;

	if( t->modified.m.title ){
		char *esc;
		if( NULL == (esc = db_escape(t->title)))
			return 1;

		len += addcom( buffer + len, SBUFLEN - len, &fields );
		len += snprintf( buffer + len, SBUFLEN - len, "title='%s'",
				esc );
		free( esc );
		if( len > SBUFLEN || len < 0 )
			return 1;
	}

	if( t->modified.m.artistid ){
		len += addcom( buffer + len, SBUFLEN - len, &fields );
		len += snprintf( buffer + len, SBUFLEN - len, "artist_id=%d", 
				t->artistid );
		if( len > SBUFLEN || len < 0 )
			return 1;
	}

	snprintf( buffer+len, SBUFLEN - len, " WHERE id = %d", t->id );
	if( len > SBUFLEN || len < 0 )
		return 1;

	res = db_query( buffer );

	if( ! res || PGRES_COMMAND_OK != PQresultStatus(res)){
		syslog( LOG_ERR, "track_save: %s", db_errstr());
		return 1;
	}

	t->modified.any = 0;
	return 0;
}


t_track *track_get( int id )
{
	char *query = NULL;
	PGresult *res;
	t_track *t;

	asprintf( &query, TRACK_QUERY "AND t.id = %d", id );
	if( NULL == query )
		return NULL;

	res = db_query( query );
	free(query);

	if( NULL == res ||  PGRES_TUPLES_OK != PQresultStatus(res)){
		syslog( LOG_ERR, "track_get: %s",
				db_errstr());
		PQclear(res);
		return NULL;
	}

	t = track_convert( res, 0 );
	PQclear( res );

	return t;
}

it_track *tracks_albumid( int albumid )
{
	char *query = NULL;
	it_db *it;

	asprintf( &query, TRACK_QUERY "AND album_id = %d "
			"ORDER BY nr", albumid );
	if( query == NULL )
		return NULL;

	it = db_iterate( query, (db_convert)track_convert );
	free(query);

	return it;
}


it_track *tracks_artistid( int artistid )
{
	char *query = NULL;
	it_db *it;

	asprintf( &query, TRACK_QUERY "AND t.artist_id = %d", artistid );
	if( query == NULL )
		return NULL;

	it = db_iterate( query, (db_convert)track_convert );
	free(query);

	return it;
}


it_track *tracks_search( const char *substr )
{
	char *str;
	char *query = NULL;
	it_db *it;

	if( NULL == (str = db_escape( substr )))
		return NULL;

	asprintf( &query, TRACK_QUERY "AND "
			"LOWER(t.title) LIKE LOWER('%%%s%%')", str );
	free(str);
	if( NULL == query )
		return NULL;

	it = db_iterate( query, (db_convert)track_convert );
	free(query);

	return it;
}

int tracks( void )
{
	PGresult *res;
	int num;

	res = db_query( "SELECT count(*) FROM " TRACK_TAB );
	if( ! res || PGRES_TUPLES_OK !=  PQresultStatus(res) ){
		syslog( LOG_ERR, "tracks: %s", db_errstr() );
		PQclear(res);
		return -1;
	}

	num = pgint(res, 0, 0 );
	PQclear(res);

	return num;
}

// TODO: dangerous to use the filter unchecked as query
// TODO: set filter asynchronously???
int random_setfilter( const char *filt )
{
	char *query = NULL;
	PGresult *res;
	char *n;

	/* flush cache table */
	res = db_query( "DROP TABLE " CACHE_TAB );
	PQclear(res);

	/* recreate empty cache table - now the filter may fail
	 * and further queries are still vaild */
	res = db_query( CACHE_CREATE );
	if (!res || PQresultStatus(res) != PGRES_COMMAND_OK){
		syslog( LOG_ERR, "setfilter: %s", db_errstr() );
		PQclear(res);
		return 1;
	}
	PQclear(res);

	/* remember filter string */
	if( NULL == (n=strdup(filt)))
		return 1;
	free( filter );
	filter = n;

	/* fill cache - if possible */
	asprintf( &query, "INSERT INTO " CACHE_TAB " "
			TRACK_QCACHE "%s%s%s",
			filt && *filt ? "AND (" : "",
			filt && *filt ? filt : "",
			filt && *filt ? ")" : ""
			);
	if( NULL == query )
		return 1;

	res = db_query( query );
	free( query );
	if( ! res || PGRES_COMMAND_OK !=  PQresultStatus(res) ){
		syslog( LOG_ERR, "setfilter: %s", db_errstr() );
		PQclear(res);
		return 1;
	}
	PQclear(res);

	if( random_func_filter )
		(*random_func_filter)();

	/* try to create index for cache table */
	res = db_query( "CREATE INDEX mserv_cache_idx "
			"ON mserv_cache(id)" );
	PQclear(res);

	return 0;
}

int random_filterstat( void )
{
	PGresult *res;
	int num;

	res = db_query( "SELECT count(*) FROM " CACHE_TAB );
	if( ! res || PGRES_TUPLES_OK !=  PQresultStatus(res) ){
		syslog( LOG_ERR, "filterstat: %s", db_errstr() );
		PQclear(res);
		return -1;
	}

	num = pgint(res, 0, 0 );
	PQclear(res);

	return num;
}

const char *random_filter( void )
{
	return filter;
}

static char *random_query( int num )
{
	char *query = NULL;

	asprintf( &query, CACHE_QUERY "ORDER BY lastplay LIMIT %d", num );
	return query;
}

it_track *random_top( int num )
{
	it_db *it;
	char *query;

	if( NULL == (query = random_query(num)) )
		return NULL;

	it = db_iterate( query, (db_convert)track_convert );
	free(query);

	return it;
}

t_track *random_fetch( void )
{
	char *query;
	PGresult *res;
	int num;
	t_track *t;

	/* get first tracks matching filter */
	if( NULL == (query = random_query(RANDOM_LIMIT)) )
		return NULL;

	res = db_query( query );
	free( query );

	if( ! res || PGRES_TUPLES_OK !=  PQresultStatus(res) ){
		syslog( LOG_ERR, "random_fetch: %s", db_errstr());
		PQclear(res);
		return NULL;
	}

	/* 
	 * randomly pick a track
	 *
	 * by adding two randoms, we get a Gaussian distribution with
	 * of random numbers between 0 and 2*RAND_MAX with it's max
	 * at RAND_MAX.
	 *
	 * We move this max to zero and take the abs() of the value.
	 *
	 * This way the likeliness of more recent tracks decreases
	 */
	num = abs( random() + random() - RAND_MAX );

	/* adjust number to available tracks */
	num = (double)num / RAND_MAX * PQntuples(res);

	t = track_convert( res, num );
	PQclear(res);

	return t;
}


