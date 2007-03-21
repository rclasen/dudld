
AM_CFLAGS=-g ${GLIB_CFLAGS} ${PSQL_CFLAGS} \
	-I.. -Wall -W -Wunused -Wmissing-prototypes -Wcast-qual -Wcast-align -Werror

noinst_PROGRAMS=test_random
test_random_LDFLAGS=-lm

noinst_LIBRARIES=libdudldb.a
libdudldb_a_SOURCES= \
	dudldb.c \
	artist.c \
	album.c \
	track.c \
	filter.c \
	user.c \
	history.c \
	queue.c \
	random.c \
	tag.c \
	sfilter.c \
	\
	dudldb.h \
	filter.h \
	queue.h \
	track.h \
	user.h
