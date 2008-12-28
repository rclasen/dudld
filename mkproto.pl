#!/usr/bin/perl -w
#
# Copyright (c) 2008 Rainer Clasen
# 
# This program is free software; you can redistribute it and/or modify
# it under the terms described in the file LICENSE included in this
# distribution.
#


use strict;

sub r_any { return "r_any" };
sub r_guest { return "r_guest" };
sub r_user { return "r_user" };
sub r_admin { return "r_admin" };
sub r_master { return "r_master" };

sub p_any { return "p_any" };
sub p_open { return "p_open" };
sub p_user { return "p_user" };
sub p_idle { return "p_idle" };

# TODO: cleanup codes
# TODO: client stuff
my @cmds = (
	{
		name	=> "quit",
		code	=> "221",
		minpriv	=> r_any,
		context	=> p_any,
		sargs	=> [qw( )],
		cargs	=> [qw( )],
		cret	=> "succ",
	},


	{
		name	=> "user",
		code	=> "320",
		minpriv	=> r_any,
		context	=> p_open,
		sargs	=> [qw( name )],
		cargs	=> [qw( name )],
		cret	=> "succ",
	},
	{
		name	=> "pass",
		code	=> "221",
		minpriv	=> r_any,
		context	=> p_user,
		sargs	=> [qw( pass )],
		cargs	=> [qw( pass )],
		cret	=> "succ",
	},


	{
		name	=> "clientlist",
		code	=> "230",
		minpriv	=> r_user,
		context	=> p_idle,
		sargs	=> [qw( )],
		cargs	=> [qw( )],
		cret	=> "it_client",
	},
	{
		name	=> "clientclose",
		code	=> "232",
		minpriv	=> r_master,
		context	=> p_idle,
		sargs	=> [qw( id )],
		cargs	=> [qw( id )],
		cret	=> "succ",
	},
	{
		name	=> "clientcloseuser",
		code	=> "231",
		minpriv	=> r_master,
		context	=> p_idle,
		sargs	=> [qw( id )],
		cargs	=> [qw( id )],
		cret	=> "succ",
	},



	{
		name	=> "userlist",
		code	=> "235",
		minpriv	=> r_user,
		context	=> p_idle,
		sargs	=> [qw( )],
		cargs	=> [qw( )],
		cret	=> "it_user",
	},
	{
		name	=> "userget",
		code	=> "233",
		minpriv	=> r_user,
		context	=> p_idle,
		sargs	=> [qw( id )],
		cargs	=> [qw( id )],
		cret	=> "user",
	},
	{
		name	=> "user2id",
		code	=> "234",
		minpriv	=> r_user,
		context	=> p_idle,
		sargs	=> [qw( name )],
		cargs	=> [qw( name )],
		cret	=> "id",
	},
	{
		name	=> "useradd",
		code	=> "238",
		minpriv	=> r_master,
		context	=> p_idle,
		sargs	=> [qw( name )],
		cargs	=> [qw( name )],
		cret	=> "id",
	},
	{
		name	=> "usersetpass",
		code	=> "236",
		minpriv	=> r_master,
		context	=> p_idle,
		sargs	=> [qw( id pass )],
		cargs	=> [qw( id pass )],
		cret	=> "succ",
	},
	{
		name	=> "usersetright",
		code	=> "237",
		minpriv	=> r_master,
		context	=> p_idle,
		sargs	=> [qw( id right )],
		cargs	=> [qw( id right )],
		cret	=> "succ",
	},
	{
		name	=> "userdel",
		code	=> "239",
		minpriv	=> r_master,
		context	=> p_idle,
		sargs	=> [qw( id )],
		cargs	=> [qw( id )],
		cret	=> "succ",
	},



	{
		name	=> "play",
		code	=> "240",
		minpriv	=> r_user,
		context	=> p_idle,
		sargs	=> [qw( )],
		cargs	=> [qw( )],
		cret	=> "succ",
	},
	{
		name	=> "stop",
		code	=> "241",
		minpriv	=> r_user,
		context	=> p_idle,
		sargs	=> [qw( )],
		cargs	=> [qw( )],
		cret	=> "succ",
	},
	{
		name	=> "next",
		code	=> "240",
		minpriv	=> r_user,
		context	=> p_idle,
		sargs	=> [qw( )],
		cargs	=> [qw( )],
		cret	=> "succ",
	},
	{
		name	=> "prev",
		code	=> "240",
		minpriv	=> r_user,
		context	=> p_idle,
		sargs	=> [qw( )],
		cargs	=> [qw( )],
		cret	=> "succ",
	},
	{
		name	=> "pause",
		code	=> "242",
		minpriv	=> r_user,
		context	=> p_idle,
		sargs	=> [qw( )],
		cargs	=> [qw( )],
		cret	=> "succ",
	},
	{
		name	=> "elapsed",
		code	=> "249",
		minpriv	=> r_guest,
		context	=> p_idle,
		sargs	=> [qw( )],
		cargs	=> [qw( )],
		cret	=> "sec",
	},
	{
		name	=> "jump",
		code	=> "248",
		minpriv	=> r_user,
		context	=> p_idle,
		sargs	=> [qw( sec )],
		cargs	=> [qw( sec )],
		cret	=> "succ",
	},
	{
		name	=> "status",
		code	=> "243",
		minpriv	=> r_guest,
		context	=> p_idle,
		sargs	=> [qw( )],
		cargs	=> [qw( )],
		cret	=> "status",
	},
	{
		name	=> "curtrack",
		code	=> "248",
		minpriv	=> r_guest,
		context	=> p_idle,
		sargs	=> [qw( )],
		cargs	=> [qw( )],
		cret	=> "track",
	},
	{
		name	=> "gap",
		code	=> "244",
		minpriv	=> r_guest,
		context	=> p_idle,
		sargs	=> [qw( )],
		cargs	=> [qw( )],
		cret	=> "sec",
	},
	{
		name	=> "gapset",
		code	=> "245",
		minpriv	=> r_user,
		context	=> p_idle,
		sargs	=> [qw( sec )],
		cargs	=> [qw( sec )],
		cret	=> "succ",
	},
	{
		name	=> "cut",
		code	=> "244",
		minpriv	=> r_guest,
		context	=> p_idle,
		sargs	=> [qw( )],
		cargs	=> [qw( )],
		cret	=> "bool",
	},
	{
		name	=> "cutset",
		code	=> "245",
		minpriv	=> r_user,
		context	=> p_idle,
		sargs	=> [qw( bool )],
		cargs	=> [qw( bool )],
		cret	=> "succ",
	},
	{
		name	=> "replaygain",
		code	=> "244",
		minpriv	=> r_guest,
		context	=> p_idle,
		sargs	=> [qw( )],
		cargs	=> [qw( )],
		cret	=> "replaygain",
	},
	{
		name	=> "replaygainset",
		code	=> "245",
		minpriv	=> r_user,
		context	=> p_idle,
		sargs	=> [qw( replaygain )],
		cargs	=> [qw( replaygain )],
		cret	=> "succ",
	},
	{
		name	=> "rgpreamp",
		code	=> "244",
		minpriv	=> r_guest,
		context	=> p_idle,
		sargs	=> [qw( )],
		cargs	=> [qw( )],
		cret	=> "decibel",
	},
	{
		name	=> "rgpreampset",
		code	=> "245",
		minpriv	=> r_user,
		context	=> p_idle,
		sargs	=> [qw( decibel )],
		cargs	=> [qw( decibel )],
		cret	=> "succ",
	},
	{
		name	=> "random",
		code	=> "246",
		minpriv	=> r_guest,
		context	=> p_idle,
		sargs	=> [qw( )],
		cargs	=> [qw( )],
		cret	=> "bool",
	},
	{
		name	=> "randomset",
		code	=> "247",
		minpriv	=> r_user,
		context	=> p_idle,
		sargs	=> [qw( bool )],
		cargs	=> [qw( bool )],
		cret	=> "succ",
	},
	{
		name	=> "sleep",
		code	=> "215",
		minpriv	=> r_guest,
		context	=> p_idle,
		sargs	=> [qw( )],
		cargs	=> [qw( )],
		cret	=> "sec",
	},
	{
		name	=> "sleepset",
		code	=> "216",
		minpriv	=> r_user,
		context	=> p_idle,
		sargs	=> [qw( sec )],
		cargs	=> [qw( sec )],
		cret	=> "succ",
	},



	{
		name	=> "trackcount",
		code	=> "214",
		minpriv	=> r_guest,
		context	=> p_idle,
		sargs	=> [qw( )],
		cargs	=> [qw( )],
		cret	=> "int",
	},
	{
		name	=> "tracksearch",
		code	=> "211",
		minpriv	=> r_guest,
		context	=> p_idle,
		sargs	=> [qw( string )],
		cargs	=> [qw( string )],
		cret	=> "it_track",
	},
	{
		name	=> "tracksearchf",
		code	=> "211",
		minpriv	=> r_guest,
		context	=> p_idle,
		sargs	=> [qw( filter )],
		cargs	=> [qw( filter )],
		cret	=> "it_track",
	},
	{
		name	=> "tracksalbum",
		code	=> "212",
		minpriv	=> r_guest,
		context	=> p_idle,
		sargs	=> [qw( id )],
		cargs	=> [qw( id )],
		cret	=> "it_track",
	},
	{
		name	=> "tracksartist",
		code	=> "213",
		minpriv	=> r_guest,
		context	=> p_idle,
		sargs	=> [qw( id )],
		cargs	=> [qw( id )],
		cret	=> "it_track",
	},
	{
		name	=> "trackget",
		code	=> "210",
		minpriv	=> r_guest,
		context	=> p_idle,
		sargs	=> [qw( id )],
		cargs	=> [qw( id )],
		cret	=> "track",
	},
	{
		name	=> "track2id",
		code	=> "214",
		minpriv	=> r_guest,
		context	=> p_idle,
		sargs	=> [qw( id id )],
		cargs	=> [qw( id id )],
		cret	=> "id",
	},
	{
		name	=> "tracksetname",
		code	=> "283",
		minpriv	=> r_admin,
		context	=> p_idle,
		sargs	=> [qw( id string )],
		cargs	=> [qw( id string )],
		cret	=> "succ",
	},
	{
		name	=> "tracksetartist",
		code	=> "284",
		minpriv	=> r_admin,
		context	=> p_idle,
		sargs	=> [qw( id id )],
		cargs	=> [qw( id id )],
		cret	=> "succ",
	},
	# TODO: track modification



	{
		name	=> "filter",
		code	=> "250",
		minpriv	=> r_guest,
		context	=> p_idle,
		sargs	=> [qw( )],
		cargs	=> [qw( )],
		cret	=> "filter",
	},
	{
		name	=> "filterset",
		code	=> "251",
		minpriv	=> r_user,
		context	=> p_idle,
		sargs	=> [qw( filter )],
		cargs	=> [qw( filter )],
		cret	=> "succ",
	},
	{
		name	=> "filterstat",
		code	=> "253",
		minpriv	=> r_guest,
		context	=> p_idle,
		sargs	=> [qw( )],
		cargs	=> [qw( )],
		cret	=> "int",
	},
	{
		name	=> "filtertop",
		code	=> "252",
		minpriv	=> r_guest,
		context	=> p_idle,
		sargs	=> [qw( num )],
		cargs	=> [qw( num )],
		cret	=> "it_track",
	},



	{
		name	=> "history",
		code	=> "260",
		minpriv	=> r_guest,
		context	=> p_idle,
		sargs	=> [qw( num )],
		cargs	=> [qw( num )],
		cret	=> "it_history",
	},
	# TODO: historysearchf
	{
		name	=> "historytrack",
		code	=> "260",
		minpriv	=> r_guest,
		context	=> p_idle,
		sargs	=> [qw( id num )],
		cargs	=> [qw( id num )],
		cret	=> "it_history",
	},



	{
		name	=> "queuelist",
		code	=> "260",
		minpriv	=> r_guest,
		context	=> p_idle,
		sargs	=> [qw( )],
		cargs	=> [qw( )],
		cret	=> "it_queue",
	},
	# TODO: queuesearchf
	{
		name	=> "queueget",
		code	=> "264",
		minpriv	=> r_user,
		context	=> p_idle,
		sargs	=> [qw( id )],
		cargs	=> [qw( id )],
		cret	=> "queue",
	},
	{
		name	=> "queueadd",
		code	=> "260",
		minpriv	=> r_user,
		context	=> p_idle,
		sargs	=> [qw( id )],
		cargs	=> [qw( id )],
		cret	=> "id",
	},
	# TODO: queueinsert
	# TODO**: queuemove
	{
		name	=> "queuedel",
		code	=> "262",
		minpriv	=> r_user,
		context	=> p_idle,
		sargs	=> [qw( id )],
		cargs	=> [qw( id )],
		cret	=> "succ",
	},
	{
		name	=> "queueclear",
		code	=> "263",
		minpriv	=> r_master,
		context	=> p_idle,
		sargs	=> [qw( )],
		cargs	=> [qw( )],
		cret	=> "succ",
	},
	{
		name	=> "queuesum",
		code	=> "265",
		minpriv	=> r_guest,
		context	=> p_idle,
		sargs	=> [qw( )],
		cargs	=> [qw( )],
		cret	=> "succ",
	},



	{
		name	=> "taglist",
		code	=> "270",
		minpriv	=> r_guest,
		context	=> p_idle,
		sargs	=> [qw( )],
		cargs	=> [qw( )],
		cret	=> "it_tag",
	},
	{
		name	=> "tagget",
		code	=> "271",
		minpriv	=> r_guest,
		context	=> p_idle,
		sargs	=> [qw( id )],
		cargs	=> [qw( id )],
		cret	=> "tag",
	},
	{
		name	=> "tagsartist",
		code	=> "270",
		minpriv	=> r_guest,
		context	=> p_idle,
		sargs	=> [qw( id )],
		cargs	=> [qw( id )],
		cret	=> "it_tag",
	},
	{
		name	=> "tag2id",
		code	=> "272",
		minpriv	=> r_guest,
		context	=> p_idle,
		sargs	=> [qw( name )],
		cargs	=> [qw( name )],
		cret	=> "id",
	},
	{
		name	=> "tagadd",
		code	=> "273",
		minpriv	=> r_admin,
		context	=> p_idle,
		sargs	=> [qw( name )],
		cargs	=> [qw( name )],
		cret	=> "id",
	},
	{
		name	=> "tagsetname",
		code	=> "274",
		minpriv	=> r_admin,
		context	=> p_idle,
		sargs	=> [qw( id name )],
		cargs	=> [qw( id name )],
		cret	=> "succ",
	},
	{
		name	=> "tagsetdesc",
		code	=> "275",
		minpriv	=> r_admin,
		context	=> p_idle,
		sargs	=> [qw( id string )],
		cargs	=> [qw( id string )],
		cret	=> "succ",
	},
	{
		name	=> "tagdel",
		code	=> "276",
		minpriv	=> r_admin,
		context	=> p_idle,
		sargs	=> [qw( id )],
		cargs	=> [qw( id )],
		cret	=> "succ",
	},


	{
		name	=> "tracktaglist",
		code	=> "277",
		minpriv	=> r_guest,
		context	=> p_idle,
		sargs	=> [qw( id )],
		cargs	=> [qw( id )],
		cret	=> "it_tag",
	},
	{
		name	=> "tracktagadd",
		code	=> "278",
		minpriv	=> r_admin,
		context	=> p_idle,
		sargs	=> [qw( id id )],
		cargs	=> [qw( id id )],
		cret	=> "succ",
	},
	{
		name	=> "tracktagdel",
		code	=> "279",
		minpriv	=> r_admin,
		context	=> p_idle,
		sargs	=> [qw( id id )],
		cargs	=> [qw( id id )],
		cret	=> "succ",
	},
	{
		name	=> "tracktagged",
		code	=> "279",
		minpriv	=> r_guest,
		context	=> p_idle,
		sargs	=> [qw( id id )],
		cargs	=> [qw( id id )],
		cret	=> "bool",
	},



	{
		name	=> "albumlist",
		code	=> "281",
		minpriv	=> r_guest,
		context	=> p_idle,
		sargs	=> [qw( )],
		cargs	=> [qw( )],
		cret	=> "it_album",
	},
	{
		name	=> "albumsartist",
		code	=> "285",
		minpriv	=> r_admin,
		context	=> p_idle,
		sargs	=> [qw( id )],
		cargs	=> [qw( id )],
		cret	=> "it_album",
	},
	{
		name	=> "albumstag",
		code	=> "285",
		minpriv	=> r_admin,
		context	=> p_idle,
		sargs	=> [qw( id )],
		cargs	=> [qw( id )],
		cret	=> "it_album",
	},
	{
		name	=> "albumsearch",
		code	=> "282",
		minpriv	=> r_guest,
		context	=> p_idle,
		sargs	=> [qw( string )],
		cargs	=> [qw( string )],
		cret	=> "it_album",
	},
	{
		name	=> "albumget",
		code	=> "280",
		minpriv	=> r_guest,
		context	=> p_idle,
		sargs	=> [qw( id )],
		cargs	=> [qw( id )],
		cret	=> "album",
	},
	# TODO: albumadd
	{
		name	=> "albumsetname",
		code	=> "283",
		minpriv	=> r_admin,
		context	=> p_idle,
		sargs	=> [qw( id string )],
		cargs	=> [qw( id string )],
		cret	=> "succ",
	},
	{
		name	=> "albumsetartist",
		code	=> "284",
		minpriv	=> r_admin,
		context	=> p_idle,
		sargs	=> [qw( id id )],
		cargs	=> [qw( id id )],
		cret	=> "succ",
	},
	{
		name	=> "albumsetyear",
		code	=> "286",
		minpriv	=> r_admin,
		context	=> p_idle,
		sargs	=> [qw( id num )],
		cargs	=> [qw( id num )],
		cret	=> "succ",
	},
	# TODO: albumdel


	{
		name	=> "artistlist",
		code	=> "291",
		minpriv	=> r_guest,
		context	=> p_idle,
		sargs	=> [qw( )],
		cargs	=> [qw( )],
		cret	=> "it_artist",
	},
	{
		name	=> "artistsearch",
		code	=> "292",
		minpriv	=> r_guest,
		context	=> p_idle,
		sargs	=> [qw( string )],
		cargs	=> [qw( string )],
		cret	=> "it_artist",
	},
	{
		name	=> "artiststag",
		code	=> "292",
		minpriv	=> r_guest,
		context	=> p_idle,
		sargs	=> [qw( id )],
		cargs	=> [qw( id )],
		cret	=> "it_artist",
	},
	{
		name	=> "artistget",
		code	=> "290",
		minpriv	=> r_guest,
		context	=> p_idle,
		sargs	=> [qw( id )],
		cargs	=> [qw( id )],
		cret	=> "artist",
	},
	{
		name	=> "artistadd",
		code	=> "295",
		minpriv	=> r_admin,
		context	=> p_idle,
		sargs	=> [qw( string )],
		cargs	=> [qw( string )],
		cret	=> "id",
	},
	{
		name	=> "artistsetname",
		code	=> "293",
		minpriv	=> r_admin,
		context	=> p_idle,
		sargs	=> [qw( id string )],
		cargs	=> [qw( id string )],
		cret	=> "succ",
	},
	{
		name	=> "artistmerge",
		code	=> "294",
		minpriv	=> r_admin,
		context	=> p_idle,
		sargs	=> [qw( id id )],
		cargs	=> [qw( id id )],
		cret	=> "succ",
	},
	{
		name	=> "artistdel",
		code	=> "295",
		minpriv	=> r_admin,
		context	=> p_idle,
		sargs	=> [qw( id )],
		cargs	=> [qw( id )],
		cret	=> "succ",
	},


	{
		name	=> "sfilterlist",
		code	=> "270",
		minpriv	=> r_guest,
		context	=> p_idle,
		sargs	=> [qw( )],
		cargs	=> [qw( )],
		cret	=> "it_sfilter",
	},
	{
		name	=> "sfilterget",
		code	=> "254",
		minpriv	=> r_guest,
		context	=> p_idle,
		sargs	=> [qw( id )],
		cargs	=> [qw( id )],
		cret	=> "sfilter",
	},
	{
		name	=> "sfilter2id",
		code	=> "255",
		minpriv	=> r_guest,
		context	=> p_idle,
		sargs	=> [qw( name )],
		cargs	=> [qw( name )],
		cret	=> "id",
	},
	{
		name	=> "sfilteradd",
		code	=> "256",
		minpriv	=> r_admin,
		context	=> p_idle,
		sargs	=> [qw( name )],
		cargs	=> [qw( name )],
		cret	=> "id",
	},
	{
		name	=> "sfiltersetname",
		code	=> "257",
		minpriv	=> r_admin,
		context	=> p_idle,
		sargs	=> [qw( id name )],
		cargs	=> [qw( id name )],
		cret	=> "succ",
	},
	{
		name	=> "sfiltersetfilter",
		code	=> "258",
		minpriv	=> r_admin,
		context	=> p_idle,
		sargs	=> [qw( id filter )],
		cargs	=> [qw( id filter )],
		cret	=> "succ",
	},
	{
		name	=> "sfilterdel",
		code	=> "259",
		minpriv	=> r_admin,
		context	=> p_idle,
		sargs	=> [qw( id )],
		cargs	=> [qw( id )],
		cret	=> "succ",
	},


	{
		name	=> "help",
		code	=> "219",
		minpriv	=> r_any,
		context	=> p_any,
		sargs	=> [qw( )],
		cargs	=> [qw( )],
		cret	=> "help",
	},
);

sub srv_argheadtpl {
	my $arg = shift;

	print "typedef void * t_arg_$arg;\n";
	print "#define arg_$arg { \"$arg\", APARSE(val_$arg), /* TPL */ AFREE(NULL) }\n";
	print "\n";
}

sub srv_argshead {
	my $cmd = shift;

	print "extern t_cmd_arg args_$cmd->{name}\[\];\n";
}

sub srv_cmdhead {
	my $cmd = shift;

	print "void cmd_$cmd->{name}( t_client *client, char *code, void **argv );\n";
}

sub srv_args {
	my $cmd = shift;

	my $args = join(", ", map { "arg_$_" } @{$cmd->{sargs}}, "end" );
	print "t_cmd_arg args_$cmd->{name}\[\]\t= { $args };\n";
}

sub srv_cmdlist {
	my $cmd = shift;
	print "\t{ \"$cmd->{name}\", \"$cmd->{code}\", $cmd->{minpriv}, $cmd->{context}, cmd_$cmd->{name}, args_$cmd->{name} },\n";
}

sub srv_cmdtpl {
	my $cmd = shift;

	print <<EOF;
void cmd_$cmd->{name}( t_client *client, char *code, void **argv )
{
EOF
	my $num = 0;
	foreach my $arg ( @{$cmd->{sargs}} ){
		print "\tt_arg_$arg\targ$num = (t_arg_$arg)argv[$num];\n";
		$num++;
	}
	if( $num ){
		print "\n";
	} else {
		print "\t(void)argv;\n";
	}
	print "\n";
	print "\t(void)client;\n";
	print "\t(void)code;\n";

	$num = 0;
	foreach my $arg ( @{$cmd->{sargs}} ){
		print "\t(void)arg$num;\n";
		$num++;
	}

	print <<EOF;

	/* TPL: cmd_$cmd->{name} */
}

EOF
}

sub cmdlist {
	my $cmd = shift;
	print $cmd->{name}, "\n";
}

sub loop {
	my $fmt = shift;

	foreach my $cmd ( @cmds ){
		&$fmt( $cmd );
	}
}

sub looparg {
	my $fmt = shift;

	my %args;
	my $arg;
	foreach my $cmd ( @cmds ){
		foreach $arg ( @{$cmd->{sargs}}, "end" ){
			$args{$arg}++;
		}
	}
	foreach $arg ( sort { $a cmp $b } keys %args ){
		&$fmt( $arg );
	}
}

my $what = shift or die "missing request";

if( $what eq "srv-argheadtpl" ){
	print "#ifndef _PROTO_ARG_H\n";
	print "#define _PROTO_ARG_H\n";
	print "#include \"proto_helper.h\"\n";
	print "#include \"proto_val.h\"\n";
	&looparg( \&srv_argheadtpl);
	print "#endif\n";

} elsif( $what eq "srv-argshead" ){
	print "#ifndef _PROTO_ARGS_H\n";
	print "#define _PROTO_ARGS_H\n";
	print "#include \"proto_helper.h\"\n";
	&loop( \&srv_argshead);
	print "#endif\n";

} elsif( $what eq "srv-cmdhead" ){
	print "#ifndef _PROTO_CMD_H\n";
	print "#define _PROTO_CMD_H\n";
	print "#include \"proto_helper.h\"\n";
	print "#include \"proto_arg.h\"\n";
	print "extern t_cmd proto_cmds[];\n";
	&loop( \&srv_cmdhead);
	print "#endif\n";


} elsif( $what eq "srv-args" ){
	print "#include \"proto_args.h\"\n";
	print "#include \"proto_arg.h\"\n";
	&loop( \&srv_args);

} elsif( $what eq "srv-cmdlist" ){
	print "#include \"proto_helper.h\"\n";
	print "#include \"proto_args.h\"\n";
	print "#include \"proto_cmd.h\"\n";
	print "t_cmd proto_cmds[] = {\n";
	&loop( \&srv_cmdlist);
	print "\t{NULL, NULL, 0, 0, NULL, NULL},\n";
	print "};\n";


} elsif( $what eq "srv-cmdtpl" ){
	print "#include \"proto_helper.h\"\n";
	print "#include \"proto_args.h\"\n";
	print "#include \"proto_cmd.h\"\n";
	&loop( \&srv_cmdtpl);

} elsif( $what eq "cmdlist" ){
	&loop( \&cmdlist );


} else {
	print STDERR "invalid request\n";
	exit 1;
}


