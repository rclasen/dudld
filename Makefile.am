AUTOMAKE_OPTIONS=foreign

CFLAGS=-g -Wall -W -Wunused -Wmissing-prototypes -Wcast-qual -Wcast-align -Werror

SUBDIRS=commondb @DBDIR@
DIST_SUBDIRS=pgdb

sbin_PROGRAMS=dudld

dudld_LDADD= commondb/libcommon.a @DBDIR@/libdudldb.a
dudld_LDFLAGS=-lpq
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
	random.h \
	sleep.h

proto.o: cmd.list
cmd.list: proto.c
	$(SHELL) mkcmdlist.sh < $< > $@

CLEANFILES=cmd.list
EXTRA_DIST=mkcmdlist.sh

