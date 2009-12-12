/*
 * Copyright (c) 2008 Rainer Clasen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms described in the file LICENSE included in this
 * distribution.
 *
 */

#include <config.h>
#include "track.h"

double track_rgval( t_track *track, t_replaygain gain )
{
	/* TODO: fall back mechanism when selected rgain isn't set */
	switch( gain ){
	  case rg_track:
		return track->rgain;

	  case rg_track_peak:
	  	return track->rgainpeak;

	  case rg_album:
	  	return track->album->rgain;

	  case rg_album_peak:
	  	return track->album->rgainpeak;

	  case rg_none:
	  	return 0;
	}
	return 0;
}




