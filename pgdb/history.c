
#define _GNU_SOURCE 1
#include <stdlib.h>
#include <syslog.h>

#include <pgdb/db.h>
#include <pgdb/track.h>
#include <history.h>

int history_add( t_track *track, int uid )
{
	char *q;
	PGresult *res;

	asprintf( &q, "INSERT INTO mserv_hist(title_id, user_id) "
			"VALUES( %d, %d )", track->id, uid );
	if( q == NULL )
		return 1;

	res = db_query( q );
	free(q);

	if( res == NULL || PQresultStatus(res) != PGRES_COMMAND_OK ){
		syslog( LOG_ERR, "history_add: %s", db_errstr() );
		PQclear(res);
		return 1;
	}

	PQclear(res);

	return 0;
}

void history_free( t_history *h)
{
	track_free(h->_track);
	free(h);
}

#define GETFIELD(var,field,gofail) \
	if( -1 == (var = PQfnumber(res, field ))){\
		syslog( LOG_ERR, "missing track data: %s", field ); \
		goto gofail; \
	}

static t_history *history_convert( PGresult *res, int tup )
{
	t_history *h;
	int f;

	if( ! res )
		return NULL;

	if( tup >= PQntuples(res) )
		return NULL;

	if( NULL == (h = malloc(sizeof(t_history))))
		return NULL;

	GETFIELD(f,"user_id", clean1 );
	h->uid = pgint(res, tup, f);

	GETFIELD(f,"played", clean1 );
	h->played = pgint(res, tup, f );

	if( NULL == ( h->_track = track_convert( res, tup )))
		goto clean1;

	return h;

clean1:
	free(h);

	return NULL;
}


t_track *history_track( t_history *h)
{
	track_use(h->_track);
	return h->_track;
}

#define HIST_QUERY	\
	"SELECT "\
		"t.id,"\
		"t.album_id,"\
		"t.nr,"\
		"date_part('epoch',f.duration) AS dur,"\
		"date_part('epoch',t.lastplay) AS lplay,"\
		"t.title,"\
		"t.artist_id,"\
		"stor_filename(u.collection,u.colnum,f.dir,f.fname) "\
			"AS filename, " \
		"date_part('epoch',h.added) AS played, "\
		"h.user_id "\
	"FROM "\
		"(( mus_title t  "\
				"INNER JOIN stor_file f  "\
				"ON t.id = f.titleid "\
			") "\
			"INNER JOIN mserv_hist h "\
			"ON t.id = h.title_id "\
		") "\
		"INNER JOIN stor_unit u  "\
		"ON f.unitid = u.id "

#define HIST_ORDER	"ORDER BY h.added DESC "

it_history *history_list( int num )
{
	char *query = NULL;
	it_db *it;

	asprintf( &query, HIST_QUERY HIST_ORDER "LIMIT %d", num );
	if( query == NULL )
		return NULL;

	it = db_iterate( query, (db_convert)history_convert );
	free(query);

	return it;
}

it_history *history_tracklist( int trackid, int num )
{
	char *query = NULL;
	it_db *it;

	asprintf( &query, HIST_QUERY "WHERE title_id = %d "
			HIST_ORDER "LIMIT %d", 
			trackid, num );
	if( query == NULL )
		return NULL;

	it = db_iterate( query, (db_convert)history_convert );
	free(query);

	return it;
}

