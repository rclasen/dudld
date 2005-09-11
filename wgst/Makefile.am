AUTOMAKE_OPTIONS=foreign

AM_CFLAGS=-I.. -g $(shell pkg-config --cflags gstreamer-0.8 glib) -Wall -W -Wunused -Wmissing-prototypes -Wcast-qual -Wcast-align -Werror
AM_LDFLAGS=$(shell pkg-config --libs gstreamer-0.8 glib)

pkglib_PROGRAMS=wgst

wgst_SOURCES=main.c ../opt.c

