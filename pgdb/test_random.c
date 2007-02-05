#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#define NUM	20

static void set( char *what, int *ar, int num )
{
	if( num < NUM ){
		++ar[num];
	} else {
		printf("%s out of bounds: %d\n", what, num );
	}
}

int main( int argc, char **argv )
{
	int plain[NUM];
	int arit[NUM];
	int geom[NUM];
	int abso[NUM];
	int divi[NUM];
	int i;

	memset(plain, 0, sizeof(int) * NUM );
	memset(arit, 0, sizeof(int) * NUM );
	memset(geom, 0, sizeof(int) * NUM );
	memset(abso, 0, sizeof(int) * NUM );
	memset(divi, 0, sizeof(int) * NUM );

	(void)argc;
	(void)argv;
	
	for( i = 1000000; --i; ){
		set( "plain", plain, ((double)random() / RAND_MAX) * NUM );
		set( "arit", arit, ( (double)random() + random() ) / ((double)2*RAND_MAX) * NUM );
		set( "geom", geom, sqrt( (double)random() * random() ) / RAND_MAX * NUM );
		set( "abso", abso, (double)abs( (double)random() + random() - RAND_MAX )  / RAND_MAX * NUM );
		set( "divi", divi, ((double)random() * random()) / ( (double)RAND_MAX * RAND_MAX) * NUM );
	}


	printf( "%3s %6s %6s %6s %6s %6s\n", "i", "plain", "arit", "geom", "abs", "div");
	for( i = 0; i < NUM; ++i ){
		printf( "%3d %6d %6d %6d %6d %6d\n", i, plain[i], arit[i], geom[i], abso[i], divi[i] );
	}

	return 0;
}
