
#include "track.h"
#include "queue.h"

// TODO: get rid of testing track
static t_track t = {
	.id = 0,
	.dir = "/pub/fun/mp3/0saug/Creed.-.Human.Clay",
	.fname = "Creed.-.02_What.If.mp3",
};

t_track *queue_fetch( void )
{
	return &t;
}

