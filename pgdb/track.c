#define _GNU_SOURCE 1
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <syslog.h>

#include <pgdb/db.h>
#include <pgdb/track.h>

#define TRACK_SELECT	" "\
	"t.id, "\
	"t.album_id, " \
	"t.nr, "\
	"date_part('epoch',f.duration) AS dur, "\
	"date_part('epoch',t.lastplay) AS lplay, "\
	"t.title, "\
	"t.artist_id, "\
	"u.collection, "\
	"u.colnum, "\
	"f.dir, "\
	"f.fname "
#if 0
#define TRACK_FROM	" "\
        "mus_title t "\
                "INNER JOIN ( "\
                        "stor_file f "\
                                "INNER JOIN stor_unit u "\
                                "ON f.unitid = u.id "\
                ") "\
                "ON t.id = f.titleid "
#else
#define TRACK_FROM	" "\
        "( "\
                "mus_title t  "\
                        "INNER JOIN stor_file f  "\
                        "ON t.id = f.titleid "\
                ")  "\
                "INNER JOIN stor_unit u  "\
                "ON f.unitid = u.id "
#endif

/* this speeds up queries enormously: */
#define TRACK_WHERE	" "\
	"f.titleid NOTNULL "\
	"AND NOT f.broken "

#define GETFIELD(var,field,gofail) \
	if( -1 == (var = PQfnumber(res, field ))){\
		syslog( LOG_ERR, "missing track data: %s", field ); \
		goto gofail; \
	}


// TODO: use something real for filter
// TODO: use temp table for caching filter results
static char *filter = NULL;

t_random_func random_func_filter = NULL;

static t_track *track_convert( PGresult *res, int tup )
{
	const char *dir, *fname;
	char *col;
	int colnum;
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

	GETFIELD(f,"colnum", clean1 );
	colnum = pgint(res, tup, f );

	GETFIELD(f,"dir", clean1 );
	dir = PQgetvalue(res, tup, f );

	GETFIELD(f,"fname", clean1 );
	fname = PQgetvalue(res, tup, f );

	GETFIELD(f,"collection", clean1 );
	col = pgstring(res, tup, f );

	t->fname = NULL;
	asprintf( &t->fname, "%s%04d/%s/%s", col, colnum, dir, fname );
	if( NULL ==  t->fname )
		goto clean2;

	GETFIELD(f,"title", clean3 );
	if( NULL == (t->title = pgstring(res, tup, f)))
		goto clean3;

	return t;

clean3:
	free(t->fname);

clean2:
	free(col);

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

int track_setlastplay( t_track *t, int lastplay )
{
	t->lastplay = lastplay;
	t->modified.m.lastplay = 1;
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

	len = snprintf( buffer, SBUFLEN, "UPDATE mus_title SET ");
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

	if( t->modified.m.lastplay ){
		len += addcom( buffer + len, SBUFLEN - len, &fields );
		len += snprintf( buffer + len, SBUFLEN - len, 
				"lastplay=timestamp 'epoch' + '%d seconds'",
				t->lastplay );
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

	asprintf( &query, "SELECT" TRACK_SELECT 
			"FROM" TRACK_FROM 
			"WHERE " TRACK_WHERE 
				" AND t.id = %d", id );
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

	asprintf( &query, "SELECT" TRACK_SELECT 
			"FROM" TRACK_FROM 
			"WHERE " TRACK_WHERE 
				" AND t.album_id = %d "
			"ORDER BY t.nr", albumid );
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

	asprintf( &query, "SELECT" TRACK_SELECT 
			"FROM" TRACK_FROM 
			"WHERE " TRACK_WHERE 
				" AND t.artist_id = %d", artistid );
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

	asprintf( &query, "SELECT" TRACK_SELECT 
			"FROM" TRACK_FROM 
			"WHERE " TRACK_WHERE " AND "
			"LOWER(t.title) LIKE LOWER('%%%s%%')", str );
	free(str);
	if( NULL == query )
		return NULL;

	it = db_iterate( query, (db_convert)track_convert );
	free(query);

	return it;
}

static char *random_query( int num, const char *filt )
{
	char *query = NULL;

	asprintf( &query, "SELECT" TRACK_SELECT 
			"FROM" TRACK_FROM 
			"WHERE " TRACK_WHERE 
			" %s %s "
			"ORDER BY t.lastplay "
			"LIMIT %d", 
			filt ? "AND" : "", 
			filt ? filt : "", 
			num );
	return query;
}

it_track *random_top( int num )
{
	it_db *it;
	char *query;

	if( NULL == (query = random_query(num, filter)) )
		return NULL;

	it = db_iterate( query, (db_convert)track_convert );
	free(query);

	return it;
}


// TODO: dangerous to use the filter unchecked as query
int random_setfilter( const char *filt )
{
	char *query;
	PGresult *res;
	char *n;

	if( ! filt || !*filt ){
		free(filter);
		filter = NULL;

		if( random_func_filter )
			(*random_func_filter)();
		return 0;
	}

	if( NULL == (query = random_query(1,filt)) )
		return 1;

	res = db_query( query );
	free( query );

	if( ! res || PGRES_TUPLES_OK !=  PQresultStatus(res) ){
		PQclear(res);
		return 1;
	}

	if( NULL == (n=strdup(filt)))
		return 1;

	free( filter );
	filter = n;

	if( random_func_filter )
		(*random_func_filter)();
	return 0;
}

const char *random_filter( void )
{
	return filter;
}


t_track *random_fetch( void )
{
	char *query;
	PGresult *res;
	int num;
	t_track *t;

	/* get first 1000 tracks matching filter */

	if( NULL == (query = random_query(1000,filter)) )
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


