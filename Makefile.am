AUTOMAKE_OPTIONS=foreign

CFLAGS=-g -Wall -W -Wunused -Wmissing-prototypes -Wcast-qual -Wcast-align -Werror

SUBDIRS=commondb @DBDIR@
DIST_SUBDIRS=$(SUBDIRS) pgdb

sbin_PROGRAMS=dudld

dudld_LDADD= commondb/libcommon.a @DBDIR@/libdudldb.a
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
EXTRA_DIST=mkcmdlist.sh

