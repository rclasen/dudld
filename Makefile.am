AUTOMAKE_OPTIONS=foreign

AM_CFLAGS=-g $(shell pkg-config --cflags gstreamer-0.8 glib) -Wall -W -Wunused -Wmissing-prototypes -Wno-cast-qual -Wcast-align

AM_LDFLAGS=$(shell pkg-config --libs gstreamer-0.8 glib)

SUBDIRS=commondb pgdb # wpipe # wgst

sbin_PROGRAMS=dudld

dudld_LDADD= commondb/libcommon.a pgdb/libdudldb.a
dudld_LDFLAGS=-lpq -lncc
dudld_SOURCES= client.c \
	main.c \
	opt.c \
	player.c \
	proto.c \
	sleep.c \
	\
	client.h \
	opt.h \
	player.h \
	proto.h \
	sleep.h

proto.o: cmd.list
cmd.list: proto.c
	sed -ne '/^CMD/s/.*\(cmd_\([a-z0-9]*\)[^)]*\).*/	{"\2", \1 },/p' $^ > $@

commands.txt: proto.c
	sed -ne 's/.*\(cmd_[a-z0-9]*\).*/\1/p' $^ | sort -u > $@
	
CLEANFILES=cmd.list

