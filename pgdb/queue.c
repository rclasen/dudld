
#include <stdlib.h>
#include <syslog.h>

#include <pgdb/track.h>
#include <pgdb/queue.h>

t_queue *queue_fetch( void )
{
	PGresult *res;

	res = db_query( "SELECT * "
			"FROM mserv_queue "
			"ORDER BY added "
			"LIMIT 1" );
	if( ! res || PQresultStatus(res) != PGRES_TUPLES_OK ){
		syslog( LOG_ERR, "queue_fetch: %s", db_errstr() );
		PQclear(res);
		return NULL;
	}

	PQclear(res);

	return NULL;
}

t_track *queue_track( t_queue *q )
{
	if( ! q )
		return NULL;

	track_use(q->_track);
	return q->_track;
}

void queue_free( t_queue *q )
{
	if( ! q )
		return;
	
	track_free(q->_track);
	free(q);
}
