
#include <stdlib.h>
#include <syslog.h>

#include "track.h"
#include "queue.h"

t_queue_func_clear queue_func_clear = NULL;
t_queue_func_fetch queue_func_add = NULL;
t_queue_func_fetch queue_func_del = NULL;
t_queue_func_fetch queue_func_fetch = NULL;

#define GETFIELD(var,field,gofail) \
	if( -1 == (var = PQfnumber(res, field ))){\
		syslog( LOG_ERR, "missing queue data: %s", field ); \
		goto gofail; \
	}

static t_queue *queue_convert( PGresult *res, int tup )
{
	t_queue *q;
	int uid;
	int f;

	if( ! res )
		return NULL;

	if( tup >= PQntuples(res) )
		return NULL;

	if( NULL == (q = malloc(sizeof(t_queue))))
		return NULL;

	q->_refs = 1;

	GETFIELD(f,"qid", clean1 );
	q->id = pgint(res, tup, f);

	GETFIELD(f,"user_id", clean1 );
	uid = pgint(res, tup, f);

	GETFIELD(f,"queued", clean1 );
	q->queued = pgint(res, tup, f );

	if( NULL == ( q->user = user_get(uid)))
		goto clean1;

	/* when there is a file_id, fetch this track seperately */
	if( -1 != (f = PQfnumber(res,"file_id"))){
		q->track = track_get(pgint(res,tup,f));

	/* otherwise try to get the data from current result */
	} else {
		q->track = track_convert( res, tup );
	}
	if( NULL == q->track )
		goto clean2;

	return q;

clean2:
	user_free(q->user);

clean1:
	free(q);

	return NULL;
}

void queue_free( t_queue *q )
{
	if( ! q )
		return;
	
	if( -- q->_refs > 0 )
		return;

	track_free(q->track);
	free(q);
}

void queue_use( t_queue *q )
{
	if( ! q )
		return;

	q->_refs ++;
}

t_track *queue_track( t_queue *q )
{
	if( ! q )
		return NULL;

	track_use(q->track);
	return q->track;
}

t_queue *queue_get( int id )
{
	PGresult *res;
	t_queue *q;

	res = db_query( "SELECT "
				"id AS qid,"
				"file_id,"
				"time2unix(added) as queued,"
				"user_id "
			"FROM mserv_queue "
			"WHERE id = %d", id );
	if( ! res || PQresultStatus(res) != PGRES_TUPLES_OK ){
		syslog( LOG_ERR, "queue_get: %s", db_errstr() );
		PQclear(res);
		return NULL;
	}

	q = queue_convert( res, 0 );
	PQclear(res);

	return q;
}

t_queue *queue_fetch( void )
{
	PGresult *res;
	t_queue *q;

	res = db_query( "SELECT "
				"id AS qid,"
				"file_id,"
				"time2unix(added) as queued,"
				"user_id "
			"FROM mserv_queue "
			"ORDER BY id "
			"LIMIT 1" );
	if( ! res || PQresultStatus(res) != PGRES_TUPLES_OK ){
		syslog( LOG_ERR, "queue_fetch: %s", db_errstr() );
		PQclear(res);
		return NULL;
	}

	q = queue_convert( res, 0 );
	PQclear(res);

	if( ! q )
		return NULL;

	res = db_query( "DELETE FROM mserv_queue WHERE id =%d", q->id);
	if( ! res || PQresultStatus(res) != PGRES_COMMAND_OK )
		syslog( LOG_ERR, "queue_fetch: %s", db_errstr() );
	PQclear(res);

	if(queue_func_fetch)
		(*queue_func_fetch)( q );
	return q;
}

it_queue *queue_list( void )
{
	return db_iterate( (db_convert)queue_convert, "SELECT "
				"q.id AS qid,"
				"time2unix(q.added) as queued,"
				"q.user_id, "
				"t.* "
			"FROM mserv_queue q "
				"INNER JOIN mserv_track t "
				"ON t.id = q.file_id "
			"ORDER BY q.id" );
}

int queue_add( int trackid, int uid )
{
	PGresult *res;
	int qid;

	res = db_query( "SELECT nextval('mserv_queue_id_seq')" );
	if( !res || PQresultStatus(res) != PGRES_TUPLES_OK ){
		syslog( LOG_ERR, "queue_add: %s", db_errstr());
		PQclear(res);
		return -1;
	}

	if( 0 > (qid = pgint(res,0,0))){
		PQclear(res);
		return -1;
	}
	PQclear(res);

	res = db_query( "INSERT INTO mserv_queue(id, file_id, user_id) "
			"VALUES( %d, %d, %d )", qid, trackid, uid );
	if( !res || PQresultStatus(res) != PGRES_COMMAND_OK ){
		syslog( LOG_ERR, "queue_add: %s", db_errstr());
		PQclear(res);
		return -1;
	}

	PQclear(res);

	if( queue_func_add ){
		t_queue *q = queue_get(qid);
		if( q != NULL ){
			(*queue_func_add)(q);
			queue_free(q);
		}
	}

	return qid;
}

int queue_del( int queueid, int uid )
{
	PGresult *res;
	t_queue *q;

	if( queue_func_del )
		q = queue_get(queueid);

	if( uid ){
		res = db_query( "DELETE FROM mserv_queue "
				"WHERE id = %d and user_id = %d", 
				queueid, uid );
	} else {
		res = db_query( "DELETE FROM mserv_queue WHERE id = %d", 
				queueid );
	}
	if( !res || PQresultStatus(res) != PGRES_COMMAND_OK ){
		syslog( LOG_ERR, "queue_del: %s", db_errstr());
		PQclear(res);
		return 1;
	}

	if( ! PQcmdTuples(res)){
		PQclear(res);
		return 1;
	}

	PQclear(res);

	if(queue_func_del && q){
		(*queue_func_del)( q );
		queue_free(q);
	}

	return 0;
}

int queue_clear( void )
{
	PGresult *res;

	res = db_query( "DELETE FROM mserv_queue" );
	if( !res || PQresultStatus(res) != PGRES_COMMAND_OK ){
		syslog( LOG_ERR, "queue_clear: %s", db_errstr());
		PQclear(res);
		return 1;
	}

	PQclear(res);

	if(queue_func_clear)
		(*queue_func_clear)();

	return 0;
}




