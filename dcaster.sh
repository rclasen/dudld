#!/bin/sh

set -e
file="$1"
gst-launch-0.8 filesrc \
		location="$file" \
	\! mad \
	\! cutter \
	\! audioscale \
	\! audioconvert \
	\! "audio/x-raw-int", \
		format=int, \
		rate=44100, \
		channels=2, \
		width=16, \
		depth=16 \
	\! udpsink \
		host=239.0.0.1 \
		port=4953 \
		control=1
