#ifndef _TRACK_H
#define _TRACK_H

typedef struct _t_track {
	int id;
	char *dir;
	char *fname;
} t_track;

t_track *random_fetch( void );

#endif
