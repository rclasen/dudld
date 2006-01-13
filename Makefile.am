AUTOMAKE_OPTIONS=foreign

# TODO: properly test for glib and gstreamer

AM_CFLAGS=-g $(shell pkg-config --cflags gstreamer-0.8 glib) -Wall -W -Wunused -Wmissing-prototypes -Wno-cast-qual -Wcast-align -DSYSCONFDIR=\"@sysconfdir@\"

AM_LDFLAGS=$(shell pkg-config --libs gstreamer-0.8 glib)

man1_MANS=dcast-client.man
man5_MANS=dudld.conf.man
man8_MANS=dudld.man

EXTRA_DIST=$(MANS)

SUBDIRS=commondb pgdb # wpipe # wgst

dist_bin_SCRIPTS=dcast-client
sysconf_DATA=dudld.conf
sbin_PROGRAMS=dudld

dudld_LDADD= commondb/libcommon.a pgdb/libdudldb.a
dudld_LDFLAGS=-lpq -lncc
dudld_SOURCES= client.c \
	main.c \
	opt.c \
	player.c \
	proto_helper.c \
	proto_fmt.c \
	proto_bcast.c \
	proto_val.c \
	proto_args.c \
	proto_cmdlist.c \
	proto_cmd.c \
	proto.c \
	sleep.c \
	\
	client.h \
	opt.h \
	player.h \
	proto_helper.h \
	proto_fmt.h \
	proto_bcast.h \
	proto_val.h \
	proto_arg.h \
	proto_args.h \
	proto_cmd.h \
	proto.h \
	sleep.h

CLEANFILES=proto_args.h \
	proto_cmd.h \
	proto_args.c \
	proto_cmdlist.c \
	proto_arg_tpl.h \
	proto_cmd_tpl.c

proto_args.h: mkproto.pl
	perl $^ srv-argshead > $@
proto_cmd.h: mkproto.pl
	perl $^ srv-cmdhead > $@

proto_args.o: proto_args.h
proto_args.c: mkproto.pl
	perl $^ srv-args > $@
proto_cmdlist.o: proto_cmd.h
proto_cmdlist.c: mkproto.pl
	perl $^ srv-cmdlist > $@


tpl: proto_arg_tpl.h proto_cmd_tpl.c
tpldiff: tpl
	diff -u proto_arg_tpl.h proto_arg.h
	diff -u proto_cmd_tpl.c proto_cmd.c

proto_arg_tpl.h: mkproto.pl
	perl $^ srv-argheadtpl > $@
proto_cmd.o: proto_cmd.h
proto_cmd_tpl.c: mkproto.pl
	perl $^ srv-cmdtpl > $@


