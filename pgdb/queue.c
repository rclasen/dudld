
#include <stdlib.h>
#include <syslog.h>

#include <pgdb/track.h>
#include <pgdb/queue.h>

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
	q->uid = pgint(res, tup, f);

	GETFIELD(f,"queued", clean1 );
	q->queued = pgint(res, tup, f );

	/* when there is a title_id, fetch this track seperately */
	if( -1 != (f = PQfnumber(res,"title_id"))){
		q->_track = track_get(pgint(res,tup,f));

	/* otherwise try to get the data from current result */
	} else {
		q->_track = track_convert( res, tup );
	}
	if( NULL == q->_track )
		goto clean1;

	return q;

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

	track_free(q->_track);
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

	track_use(q->_track);
	return q->_track;
}

t_queue *queue_get( int id )
{
	PGresult *res;
	t_queue *q;

	res = db_query( "SELECT "
				"id AS qid,"
				"title_id,"
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
				"title_id,"
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
				"ON t.id = q.title_id "
			"ORDER BY id" );
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

	res = db_query( "INSERT INTO mserv_queue(id, title_id, user_id) "
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

int queue_del( int queueid )
{
	PGresult *res;
	t_queue *q;

	if( queue_func_del )
		q = queue_get(queueid);

	res = db_query( "DELETE FROM mserv_queue WHERE id = %d", queueid );
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

	if(queue_func_del){
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




