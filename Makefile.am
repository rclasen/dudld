
SUBDIRS=commondb @DBDIR@
DIST_SUBDIRS=pgdb

sbin_PROGRAMS=dudld
noinst_PROGRAMS=testfilt

dudld_LDADD=@DBDIR@/libdudldb.a
dudld_LDFLAGS=-lpq
dudld_SOURCES= client.c \
	filter.c \
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

cmd.list: proto.c
	$(SHELL) mkcmdlist.sh < $< > $@

testfilt_SOURCES=testfilt.c \
	filter_defs.c \
	filter_parse.y \
	filter_defs.h

CLEANFILES=cmd.list filter_parse.c filter_parse.h
EXTRA_DIST=mkcmdlist.sh

