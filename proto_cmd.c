#include <string.h>
#include <syslog.h>

#include "sleep.h"
#include "proto_helper.h"
#include "proto_cmd.h"
#include "proto_args.h"
#include "proto_fmt.h"
#include "proto_bcast.h"

void cmd_quit( t_client *client, char *code, void **argv )
{

	(void)argv;
	proto_rlast( client, code, "bye");
	client_close( client );
}

void cmd_user( t_client *client, char *code, void **argv )
{
	t_arg_name	user = (t_arg_name)argv[0];

	if( client->pdata )
		free(client->pdata);

	client->pdata = strdup( user );
	client->pstate = p_user;

	proto_rlast( client, code, "user ok, use PASS for password");
}

void cmd_pass( t_client *client, char *code, void **argv )
{
	t_arg_pass	pass = (t_arg_pass)argv[0];

	if( NULL == (client->user = user_getn( client->pdata )))
		goto clean1;

	if( ! user_ok( client->user, pass ))
		goto clean2;

	client->pstate = p_idle;

	syslog( LOG_INFO, "con #%d: user %s logged in",
			client->id, client->user->name );
	proto_rlast( client, code, "successfully logged in" );
	proto_bcast_login( client );
	
	goto final;

clean2:
	user_free( client->user );
	client->user = NULL;

clean1:
	client->pstate = p_open;
	syslog( LOG_NOTICE, "con #%d: login failed", client->id );
	proto_rlast( client, "501", "login failed" );

final:
	free( client->pdata );
	client->pdata = NULL;
}

void cmd_clientlist( t_client *client, char *code, void **argv )
{
	it_client *it;

	(void)argv;
	it = clients_list();
	dump_clients(client,code,it);
	it_client_done(it);
}

void cmd_clientclose( t_client *client, char *code, void **argv )
{
	t_arg_id	id = (t_arg_id)argv[0];
	t_client	*c;

	if( NULL == (c = client_get( id ))){
		proto_rlast( client, "501", "session not found");
		return;
	}
	proto_rlast( c, "632", "disconnected" ); 
	client_close( c );
	client_delref( c );

	proto_rlast( client, code, "disconnected" );
}

void cmd_clientcloseuser( t_client *client, char *code, void **argv )
{
	t_arg_id	id = (t_arg_id)argv[0];
	it_client *it;
	t_client *t;
	int found = 0;

	if( NULL == (it = clients_uid(id))){
		proto_rlast(client, "501", "failed to get session list" );
		return;
	}

	for( t=it_client_begin(it); t; t=it_client_next(it)){
		proto_rlast(t, "632", "kicked" );
		client_close(t);
		client_delref(t);
		found++;
	}
	it_client_done(it);

	if( found )
		proto_rlast(client, code, "kicked");
	else
		proto_rlast(client, "501", "user not found");

}

void cmd_userlist( t_client *client, char *code, void **argv )
{
	it_user *it;

	(void)argv;

	it = users_list();
	dump_users( client, code, it );
	it_user_done( it );
}

void cmd_userget( t_client *client, char *code, void **argv )
{
	t_arg_id	id = (t_arg_id)argv[0];
	t_user *u;

	if( NULL == (u = user_get(id))){
		proto_rlast(client, "501", "user not found" );
		return;
	}
	
	dump_user(client, code, u);
	user_free(u);
}

void cmd_user2id( t_client *client, char *code, void **argv )
{
	t_arg_name	user = (t_arg_name)argv[0];
	int uid;

	if( 0 > ( uid = user_id(user))){
		proto_rlast(client,"501","no such user");
		return;
	}

	proto_rlast(client, code, "%d", uid);
}

void cmd_useradd( t_client *client, char *code, void **argv )
{
	t_arg_name	user = (t_arg_name)argv[0];
	int uid;

	if( 0 > ( uid = user_add(user, 1, ""))){
		proto_rlast(client, "530", "failed" );
		return;
	} 

	proto_rlast(client, code, "%d", uid );
}

void cmd_usersetpass( t_client *client, char *code, void **argv )
{
	t_arg_id	id = (t_arg_id)argv[0];
	t_arg_pass	pass = (t_arg_pass)argv[1];

	if( 0 > user_setpass(id, pass) ){
		proto_rlast(client, "530", "failed");
	} else {
		proto_rlast(client, code, "password changed" );
	}
}

void cmd_usersetright( t_client *client, char *code, void **argv )
{
	t_arg_id	id = (t_arg_id)argv[0];
	t_arg_right	right = (t_arg_right)argv[1];

	if( 0 > user_setright(id, right) ){
		proto_rlast(client, "530", "failed");
	} else {
		proto_rlast(client, code, "right changed" );
	}
}

void cmd_userdel( t_client *client, char *code, void **argv )
{
	t_arg_id	id = (t_arg_id)argv[0];

	if( 0 > user_del(id)){
		proto_rlast(client, "530", "failed" );
		return;
	}

	proto_rlast(client, code, "deleted");
}

void cmd_play( t_client *client, char *code, void **argv )
{
	(void)argv;
	proto_player_reply( client, player_start(), code, "playing");
}

void cmd_stop( t_client *client, char *code, void **argv )
{
	(void)argv;
	proto_player_reply( client, player_stop(), code, "stopped");
}

void cmd_next( t_client *client, char *code, void **argv )
{
	(void)argv;
	proto_player_reply( client, player_next(), code, "playing next");
}

void cmd_prev( t_client *client, char *code, void **argv )
{
	(void)argv;
	proto_player_reply( client, player_prev(), code, "playing previous");
}

void cmd_pause( t_client *client, char *code, void **argv )
{
	(void)argv;
	proto_player_reply( client, player_pause(), code, "paused");
}

void cmd_elapsed( t_client *client, char *code, void **argv )
{
	(void)argv;
	proto_rlast(client, code, "%d", player_elapsed());
}

void cmd_jump( t_client *client, char *code, void **argv )
{
	t_arg_sec	sec = (t_arg_sec)argv[0];

	proto_player_reply( client, player_jump(sec), code, "jumped");
}

void cmd_status( t_client *client, char *code, void **argv )
{
	(void)argv;
	proto_rlast(client, code, "%d", player_status() );
}

void cmd_curtrack( t_client *client, char *code, void **argv )
{
	t_track *t;

	(void)argv;

	if( NULL == (t = player_track())){
		proto_rlast(client,"541", "nothing playing (maybe in a gap?)" );
		return;
	}
	dump_track(client,code,t);
	track_free(t);
}

void cmd_gap( t_client *client, char *code, void **argv )
{
	(void)argv;
	proto_rlast(client, code, "%d", player_gap() );
}

void cmd_gapset( t_client *client, char *code, void **argv )
{
	t_arg_sec	gap = (t_arg_sec)argv[0];

	player_setgap( gap );
	proto_rlast(client, code, "gap adjusted" );
}

void cmd_cut( t_client *client, char *code, void **argv )
{
	(void)argv;
	proto_rlast(client, code, "%d", player_cut() );
}

void cmd_cutset( t_client *client, char *code, void **argv )
{
	t_arg_bool	arg0 = (t_arg_bool)argv[0];

	player_setcut( arg0 );
	proto_rlast(client, code, "cutting adjusted" );
}

void cmd_replaygain( t_client *client, char *code, void **argv )
{
	(void)argv;
	proto_rlast(client, code, "%d", player_rgtype() );
}

void cmd_replaygainset( t_client *client, char *code, void **argv )
{
	t_arg_replaygain	arg0 = (t_arg_replaygain)argv[0];

	player_setrgtype( arg0 );
	proto_rlast(client, code, "replaygain type adjusted" );
}

void cmd_rgpreamp( t_client *client, char *code, void **argv )
{
	(void)argv;
	proto_rlast(client, code, "%f", player_rgpreamp() );
}

void cmd_rgpreampset( t_client *client, char *code, void **argv )
{
	t_arg_decibel	arg0 = (t_arg_decibel)argv[0];

	player_setrgpreamp( *arg0 );
	proto_rlast(client, code, "replaygain preamplification adjusted" );
}

void cmd_random( t_client *client, char *code, void **argv )
{
	(void)argv;
	proto_rlast(client, code, "%d", player_random() );
}

void cmd_randomset( t_client *client, char *code, void **argv )
{
	t_arg_bool	arg0 = (t_arg_bool)argv[0];

	player_setrandom( arg0 );
	proto_rlast(client, code, "random adjusted" );
}

void cmd_sleep( t_client *client, char *code, void **argv )
{
	(void)argv;
	proto_rlast(client, code, "%d", sleep_remain() );
}

void cmd_sleepset( t_client *client, char *code, void **argv )
{
	t_arg_sec	sec = (t_arg_sec)argv[0];

	sleep_in( sec );
	proto_rlast(client, code, "ok, will stop in %d seconds", sec);
}

void cmd_trackcount( t_client *client, char *code, void **argv )
{
	int matches;

	(void)argv;

	if( 0 > ( matches = tracks())){
		proto_rlast(client, "510", "internal error" );
		return;
	}

	proto_rlast(client, code, "%d", matches );
}

void cmd_tracksearch( t_client *client, char *code, void **argv )
{
	t_arg_string	pat = (t_arg_string)argv[0];
	it_track *it;

	it = tracks_search(pat);
	dump_tracks( client, code, it );
	it_track_done(it);
}

void cmd_tracksearchf( t_client *client, char *code, void **argv )
{
	t_arg_filter	filter = (t_arg_filter)argv[0];
	expr *e = NULL;
	char *msg;
	int pos;
	it_track *it;

	if( NULL == (e = expr_parse_str( &pos, &msg, filter ))){
		proto_rlast(client, "511", "error at pos %d in filter: %s", pos, msg );
		return;
	}

	it = tracks_searchf( e );
	dump_tracks( client, code, it );
	it_track_done(it);

	expr_free(e);
}

void cmd_tracksalbum( t_client *client, char *code, void **argv )
{
	t_arg_id	id = (t_arg_id)argv[0];
	it_track *it;

	it = tracks_albumid(id);
	dump_tracks( client, code, it );
	it_track_done(it);
}

void cmd_tracksartist( t_client *client, char *code, void **argv )
{
	t_arg_id	id = (t_arg_id)argv[0];
	it_track *it;

	it = tracks_artistid(id);
	dump_tracks( client, code, it );
	it_track_done(it);
}

void cmd_trackget( t_client *client, char *code, void **argv )
{
	t_arg_id	id = (t_arg_id)argv[0];
	t_track *t;

	if( NULL == (t = track_get( id ))){
		proto_rlast(client,"511", "no such track" );
		return;
	}

	dump_track(client,code,t);
	track_free(t);
}

void cmd_track2id( t_client *client, char *code, void **argv )
{
	t_arg_id	id = (t_arg_id)argv[0];
	t_arg_id	pos = (t_arg_id)argv[1];

	if( 0 > (id = track_id( id, pos))){
		proto_rlast(client,"501", "no such track found" );
		return;
	}

	proto_rlast(client, code, "%d", id );
}

void cmd_tracksetname( t_client *client, char *code, void **argv )
{
	t_arg_id	id = (t_arg_id)argv[0];
	t_arg_string	name = (t_arg_string)argv[1];

	if( track_setname(id, name )){
		proto_rlast(client,"511", "failed" );
		return;
	}

	proto_rlast(client,code, "name changed" );
}

void cmd_tracksetartist( t_client *client, char *code, void **argv )
{
	t_arg_id	track = (t_arg_id)argv[0];
	t_arg_id	artist = (t_arg_id)argv[1];

	if( track_setartist(track, artist)){
		proto_rlast(client,"511", "failed" );
		return;
	}

	proto_rlast(client,code, "artist changed" );
}

void cmd_filter( t_client *client, char *code, void **argv )
{
	char buf[1024];
	expr *e;

	(void)argv;
	e = random_filter();
	if( e )
		expr_fmt( buf, 1024, e );

	proto_rlast(client, code, "%s", e ? buf : "" );
}

void cmd_filterset( t_client *client, char *code, void **argv )
{
	t_arg_filter	filter = (t_arg_filter)argv[0];
	expr *e = NULL;
	char *msg;
	int pos;

	if( NULL == (e = expr_parse_str( &pos, &msg, filter ))){
		proto_rlast(client, "511", "error at pos %d in filter: %s",
				pos, msg );
		return;
	}

	/* at least initialize with an empty filter */
	if( random_setfilter(e)){
		proto_rlast(client, "511", "failed to apply (correct) filter" );
	} else {
		proto_rlast(client, code, "filter changed" );
	}
	expr_free(e);
}

void cmd_filterstat( t_client *client, char *code, void **argv )
{
	int matches;

	(void)argv;

	if( 0 > ( matches = random_filterstat())){
		proto_rlast(client, "511", "filter error" );
		return;
	}

	proto_rlast(client, code, "%d", matches );
}

void cmd_filtertop( t_client *client, char *code, void **argv )
{
	t_arg_num	num = (t_arg_num)argv[0];
	it_track *it;

	it = random_top(num);
	dump_tracks( client, code, it );
	it_track_done(it);
}

void cmd_history( t_client *client, char *code, void **argv )
{
	t_arg_num	num = (t_arg_num)argv[0];
	it_history *it;

	it = history_list( num );
	dump_history( client, code, it );
	it_history_done(it);
}

void cmd_historytrack( t_client *client, char *code, void **argv )
{
	t_arg_id	id = (t_arg_id)argv[0];
	t_arg_num	num = (t_arg_num)argv[1];
	it_history *it;

	it = history_tracklist( id, num );
	dump_history( client, code, it );
	it_history_done(it);
}

void cmd_queuelist( t_client *client, char *code, void **argv )
{
	it_queue *it;

	(void)argv;
	it = queue_list();
	dump_queues( client, code, it );
	it_queue_done(it);
}

void cmd_queueget( t_client *client, char *code, void **argv )
{
	t_arg_id	id = (t_arg_id)argv[0];
	t_queue *q;

	if( NULL == (q = queue_get(id))){
		proto_rlast(client,"501", "queue entry not found" );
		return;
	}

	dump_queue(client,code,q);
	queue_free(q);
}

void cmd_queueadd( t_client *client, char *code, void **argv )
{
	t_arg_id	id = (t_arg_id)argv[0];
	int qid;

	if( -1 == (qid = queue_add(id, client->user->id))){
		proto_rlast(client, "561", "failed to add track to queue" );
		return;
	}

	proto_rlast(client, code, "%d", qid );
}

void cmd_queuedel( t_client *client, char *code, void **argv )
{
	t_arg_id	id = (t_arg_id)argv[0];
	int uid;

	uid = client->user->id;
	if( client->user->right == r_master )
		uid = 0;

	if( queue_del( id, uid )){
		proto_rlast(client,"562", "failed to delete from queue" );
		return;
	}

	proto_rlast(client,code, "track removed from queue" );
}

void cmd_queueclear( t_client *client, char *code, void **argv )
{
	(void)argv;

	if( queue_clear() ){
		proto_rlast(client,"563", "failed to clear queue" );
		return;
	}

	proto_rlast(client,code, "queue cleared" );
}

void cmd_queuesum( t_client *client, char *code, void **argv )
{
	int sum;

	(void)argv;

	if( 0 > ( sum = queue_sum())){
		proto_rlast(client, "510", "internal error" );
		return;
	}

	proto_rlast(client, code, "%d", sum );
}

void cmd_taglist( t_client *client, char *code, void **argv )
{
	it_tag *it;

	(void)argv;
	it = tags_list();
	dump_tags( client, code, it );
	it_tag_done(it );
}

void cmd_tagget( t_client *client, char *code, void **argv )
{
	t_arg_id	id = (t_arg_id)argv[0];
	t_tag *t;

	if( NULL == (t = tag_get( id ))){
		proto_rlast(client,"511", "no such tag" );
		return;
	}

	dump_tag(client, code, t);
	tag_free(t);
}

void cmd_tagsartist( t_client *client, char *code, void **argv )
{
	t_arg_id	aid = (t_arg_id)argv[0];
	it_tag *it;

	it = tags_artist(aid);
	dump_tags( client, code, it );
	it_tag_done(it );
}

void cmd_tag2id( t_client *client, char *code, void **argv )
{
	t_arg_name	name = (t_arg_name)argv[0];
	int t;

	if( 0 > (t = tag_id( name ))){
		proto_rlast(client,"511", "no such tag" );
		return;
	}
	proto_rlast(client,code, "%d", t);
}

void cmd_tagadd( t_client *client, char *code, void **argv )
{
	t_arg_name	name = (t_arg_name)argv[0];
	int id;

	if( 0 > (id = tag_add( name ))){
		proto_rlast(client,"511", "failed" );
		return;
	}

	proto_rlast(client, code, "%d", id );
}

void cmd_tagsetname( t_client *client, char *code, void **argv )
{
	t_arg_id	id = (t_arg_id)argv[0];
	t_arg_name	name = (t_arg_name)argv[1];

	if( tag_setname(id, name )){
		proto_rlast(client,"511", "failed" );
		return;
	}

	proto_rlast(client,code, "name changed" );
}

void cmd_tagsetdesc( t_client *client, char *code, void **argv )
{
	t_arg_id	id = (t_arg_id)argv[0];
	t_arg_string	desc = (t_arg_string)argv[1];

	if( tag_setdesc(id, desc )){
		proto_rlast(client,"511", "failed" );
		return;
	}

	proto_rlast(client,code, "desc changed" );
}

void cmd_tagdel( t_client *client, char *code, void **argv )
{
	t_arg_id	id = (t_arg_id)argv[0];

	if( tag_del( id )){
		proto_rlast(client,"511", "failed" );
		return;
	}

	proto_rlast(client,code, "deleted" );
}

void cmd_tracktaglist( t_client *client, char *code, void **argv )
{
	t_arg_id	id = (t_arg_id)argv[0];
	it_tag *it;

	it = track_tags(id);
	dump_tags( client, code, it );
	it_tag_done(it );
}

void cmd_tracktagadd( t_client *client, char *code, void **argv )
{
	t_arg_id	trackid = (t_arg_id)argv[0];
	t_arg_id	tagid = (t_arg_id)argv[1];

	if( track_tagadd(trackid,tagid)){
		proto_rlast(client,"511", "failed" );
		return;
	}

	proto_rlast(client,code, "tag added to track (or already exists)" );
}

void cmd_tracktagdel( t_client *client, char *code, void **argv )
{
	t_arg_id	trackid = (t_arg_id)argv[0];
	t_arg_id	tagid = (t_arg_id)argv[1];

	if( track_tagdel(trackid,tagid)){
		proto_rlast(client,"511", "failed" );
		return;
	}

	proto_rlast(client,code, "tag deleted from track" );
}

void cmd_tracktagged( t_client *client, char *code, void **argv )
{
	t_arg_id	trackid = (t_arg_id)argv[0];
	t_arg_id	tagid = (t_arg_id)argv[1];
	int r;

	if( 0 > (r = track_tagged(trackid,tagid))){
		proto_rlast(client,"511", "failed" );
		return;
	}

	proto_rlast(client,code, "%d", r );
}

void cmd_albumlist( t_client *client, char *code, void **argv )
{
	it_album *it;

	(void)argv;
	it = albums_list( );
	dump_albums( client, code, it );
	it_album_done(it);
}

void cmd_albumsartist( t_client *client, char *code, void **argv )
{
	t_arg_id	id = (t_arg_id)argv[0];
	it_album *it;

	it = albums_artistid(id);
	dump_albums( client, code, it );
	it_album_done(it);
}

void cmd_albumstag( t_client *client, char *code, void **argv )
{
	t_arg_id	tid = (t_arg_id)argv[0];
	it_album *it;

	it = albums_tag(tid);
	dump_albums( client, code, it );
	it_album_done(it);
}

void cmd_albumsearch( t_client *client, char *code, void **argv )
{
	t_arg_string	pat = (t_arg_string)argv[0];
	it_album *it;

	it = albums_search(pat);
	dump_albums( client, code, it );
	it_album_done(it);
}

void cmd_albumget( t_client *client, char *code, void **argv )
{
	t_arg_id	id = (t_arg_id)argv[0];
	t_album *t;

	if( NULL == (t = album_get(id))){
		proto_rlast(client, "580", "failed" );
		return;
	}

	dump_album(client,code, t);
	album_free(t);
}

void cmd_albumsetname( t_client *client, char *code, void **argv )
{
	t_arg_id	id = (t_arg_id)argv[0];
	t_arg_string	name = (t_arg_string)argv[1];

	if( album_setname(id, name )){
		proto_rlast(client,"511", "failed" );
		return;
	}

	proto_rlast(client,code, "name changed" );
}

void cmd_albumsetartist( t_client *client, char *code, void **argv )
{
	t_arg_id	album = (t_arg_id)argv[0];
	t_arg_id	artist = (t_arg_id)argv[1];

	if( album_setartist(album, artist)){
		proto_rlast(client,"511", "failed" );
		return;
	}

	proto_rlast(client,code, "artist changed" );
}

void cmd_albumsetyear( t_client *client, char *code, void **argv )
{
	t_arg_id	album = (t_arg_id)argv[0];
	t_arg_num	year = (t_arg_num)argv[1];

	if( album_setyear(album, year)){
		proto_rlast(client,"511", "failed" );
		return;
	}

	proto_rlast(client,code, "year changed" );
}

void cmd_artistlist( t_client *client, char *code, void **argv )
{
	it_artist *it;

	(void)argv;
	it = artists_list( );
	dump_artists( client, code, it );
	it_artist_done(it);
}

void cmd_artistsearch( t_client *client, char *code, void **argv )
{
	t_arg_string	pat = (t_arg_string)argv[0];
	it_artist *it;

	it = artists_search( pat );
	dump_artists( client, code, it );
	it_artist_done(it);
}

void cmd_artiststag( t_client *client, char *code, void **argv )
{
	t_arg_id	tid = (t_arg_id)argv[0];
	it_artist *it;

	it = artists_tag( tid );
	dump_artists( client, code, it );
	it_artist_done(it);
}

void cmd_artistget( t_client *client, char *code, void **argv )
{
	t_arg_id	id = (t_arg_id)argv[0];
	t_artist *a;

	if( NULL == (a = artist_get(id))){
		proto_rlast(client, "590", "failed" );
		return;
	}
	dump_artist(client,code,a);
	artist_free(a);
}

void cmd_artistadd( t_client *client, char *code, void **argv )
{
	t_arg_string	name = (t_arg_string)argv[0];
	int id;

	if( 0 > (id = artist_add( name ))){
		proto_rlast(client,"511", "failed" );
		return;
	}

	proto_rlast(client, code, "%d", id );
}

void cmd_artistsetname( t_client *client, char *code, void **argv )
{
	t_arg_id	id = (t_arg_id)argv[0];
	t_arg_string	name = (t_arg_string)argv[1];

	if( artist_setname(id, name )){
		proto_rlast(client,"511", "failed" );
		return;
	}

	proto_rlast(client,code, "name changed" );
}

void cmd_artistmerge( t_client *client, char *code, void **argv )
{
	t_arg_id	from = (t_arg_id)argv[0];
	t_arg_id	to = (t_arg_id)argv[1];

	if( artist_merge(from, to )){
		proto_rlast(client,"511", "failed" );
		return;
	}

	proto_rlast(client,code, "merged artist %d into %d", from, to );
}

void cmd_artistdel( t_client *client, char *code, void **argv )
{
	t_arg_id	id = (t_arg_id)argv[0];

	if( artist_del(id)){
		proto_rlast(client,"511", "failed" );
		return;
	}

	proto_rlast(client,code, "artist deleted" );
}

void cmd_sfilterlist( t_client *client, char *code, void **argv )
{
	it_sfilter *it;

	(void)argv;
	it = sfilters_list();
	dump_sfilters( client, code, it );
	it_sfilter_done(it );
}

void cmd_sfilterget( t_client *client, char *code, void **argv )
{
	t_arg_id	id = (t_arg_id)argv[0];
	t_sfilter *t;

	if( NULL == (t = sfilter_get( id ))){
		proto_rlast(client,"511", "no such sfilter" );
		return;
	}

	dump_sfilter(client,code, t); 
	sfilter_free(t);
}

void cmd_sfilter2id( t_client *client, char *code, void **argv )
{
	t_arg_name	name = (t_arg_name)argv[0];
	int t;

	if( 0 > (t = sfilter_id( name ))){
		proto_rlast(client,"511", "no such sfilter" );
		return;
	}
	proto_rlast(client,code, "%d", t);
}

void cmd_sfilteradd( t_client *client, char *code, void **argv )
{
	t_arg_name	name = (t_arg_name)argv[0];
	int id;

	if( 0 > (id = sfilter_add( name ))){
		proto_rlast(client,"511", "failed" );
		return;
	}

	proto_rlast(client, code, "%d", id );
}

void cmd_sfiltersetname( t_client *client, char *code, void **argv )
{
	t_arg_id	id = (t_arg_id)argv[0];
	t_arg_name	name = (t_arg_name)argv[1];

	if( sfilter_setname(id, name )){
		proto_rlast(client,"511", "failed" );
		return;
	}

	proto_rlast(client,code, "name changed" );
}

void cmd_sfiltersetfilter( t_client *client, char *code, void **argv )
{
	t_arg_id	id = (t_arg_id)argv[0];
	t_arg_filter	filter = (t_arg_filter)argv[1];
	expr *e = NULL;
	char *msg;
	int pos;

	if( NULL == (e = expr_parse_str( &pos, &msg, filter ))){
		proto_rlast(client, "511", "error at pos %d in filter: %s", pos, msg );
		return;
	}
	expr_free(e);

	if( sfilter_setfilter(id, filter )){
		proto_rlast(client,"511", "failed" );
	} else {
		proto_rlast(client,code, "filter changed" );
	}
}

void cmd_sfilterdel( t_client *client, char *code, void **argv )
{
	t_arg_id	id = (t_arg_id)argv[0];

	if( sfilter_del( id )){
		proto_rlast(client,"511", "failed" );
		return;
	}

	proto_rlast(client,code, "deleted" );
}

void cmd_help( t_client *client, char *code, void **argv )
{
	t_cmd *cmd;
	t_rights perm;

	(void)argv;
	perm = client->user ? client->user->right : r_any;
	for( cmd = proto_cmds; cmd && cmd->name; cmd++ ){
		if( cmd->context != p_any && cmd->context != client->pstate )
			continue;

		if( cmd->perm > perm )
			continue;

		proto_rline(client,code,cmd->name);
	}
	proto_rlast(client,code,"");
}

