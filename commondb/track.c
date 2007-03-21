#include <config.h>
#include "track.h"

double track_rgval( t_track *track, t_replaygain gain )
{
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




