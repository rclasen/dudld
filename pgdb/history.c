
#include <stdlib.h>
#include <syslog.h>
#include <time.h>

#include <commondb/history.h>
#include <commondb/random.h>
#include "dudldb.h"
#include "track.h"

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

	track_free(h->track);
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

	GETFIELD(f,"played", clean1 );
	h->played = pgint(res, tup, f );

	GETFIELD(f,"user_id", clean1 );
	if( NULL == ( h->user = user_get(pgint(res, tup, f))))
		goto clean1;

	if( NULL == ( h->track = track_convert( res, tup )))
		goto clean2;

	return h;

clean2:
	user_free(h->user);

clean1:
	free(h);

	return NULL;
}


t_track *history_track( t_history *h)
{
	track_use(h->track);
	return h->track;
}

// TODO: use view
it_history *history_list( int num )
{
	return db_iterate( (db_convert)history_convert, 
			"SELECT "
				"t.*,"
        			"time2unix(h.added) AS played,"
				"h.user_id "
			"FROM "
				"( SELECT * FROM mserv_hist "
					"ORDER BY added DESC "
					"LIMIT %d "
				") AS h "
					"INNER JOIN mserv_track t "
					"ON t.id = h.file_id "
			"ORDER BY h.added ", 
			num );
}

// TODO: use view
it_history *history_tracklist( int trackid, int num )
{
	return db_iterate( (db_convert)history_convert,
			"SELECT "
				"t.*,"
        			"time2unix(h.added) AS played,"
				"h.user_id "
			"FROM "
				"( SELECT * FROM mserv_hist "
					"WHERE id = %d "
					"ORDER BY added DESC "
					"LIMIT %d "
				") AS h "
					"INNER JOIN mserv_track t "
					"ON t.id = h.file_id "
			"ORDER BY h.added DESC ",
					
			trackid, num );
}

