
#include <stdlib.h>
#include <syslog.h>
#include <time.h>

#include <pgdb/db.h>
#include <pgdb/track.h>
#include <random.h>
#include <history.h>

int history_add( t_track *track, int uid )
{
	PGresult *res;
	time_t now;

	now = time(NULL);
	res = db_query( "INSERT INTO mserv_hist(file_id, user_id, added) "
			"VALUES( %d, %d, unix2time(%d) )", 
			track->id, uid, now );
	if( res == NULL || PQresultStatus(res) != PGRES_COMMAND_OK ){
		syslog( LOG_ERR, "history_add: %s", db_errstr() );
		PQclear(res);
		return 1;
	}

	PQclear(res);

	random_cache_update( track->id, now );

	return 0;
}

void history_free( t_history *h)
{
	if( !h )
		return;

	track_free(h->_track);
	free(h);
}

#define GETFIELD(var,field,gofail) \
	if( -1 == (var = PQfnumber(res, field ))){\
		syslog( LOG_ERR, "missing history data: %s", field ); \
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

// TODO: history_*list() are *SLOW*
it_history *history_list( int num )
{
	return db_iterate( (db_convert)history_convert, "SELECT * "
			"FROM mserv_xhist "
			"ORDER BY played DESC "
			"LIMIT %d", num );
}

it_history *history_tracklist( int trackid, int num )
{
	return db_iterate( (db_convert)history_convert, "SELECT * "
			"FROM mserv_xhist "
			"WHERE id = %d "
			"ORDER BY played DESC "
			"LIMIT %d", 
			trackid, num );
}

