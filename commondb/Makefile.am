
AM_CFLAGS=-g ${GLIB_CFLAGS} \
	-Wall -W -Wunused -Wmissing-prototypes -Wcast-qual -Wcast-align -Werror

noinst_LIBRARIES=libcommon.a
noinst_PROGRAMS=testparse

libcommon_a_SOURCES= parsebuf.c \
	track.c \
	parseexpr.c \
	\
	parsebuf.h \
	parseexpr.h

testparse_LDADD=libcommon.a

noinst_HEADERS= album.h \
	artist.h \
	dudldb.h \
	history.h \
	queue.h \
	random.h \
	tag.h \
	track.h \
	user.h
