
#include <stdio.h>
#include <stdlib.h>

#include "parseexpr.h"

int main( int argc, char **argv )
{
	char buf[1024];
	parser_input *pi;
	expr *e;
	char *err = NULL;
	int pos;


	if( argc != 2 ){
		fprintf( stderr,"usage: %s <string>\n", argv[0] );
		exit(1);
	}

	pi = pi_str_new( argv[1] );
	if( NULL == (e = expr_parse( NULL, &pos, &err, pi ))){
		printf( "error at %d: %s\n", pos, err );
		printf( "%s\n", argv[1] );
		for(--pos; pos > 0; --pos ) printf( " " );
		printf( "^\n" );
		exit(1);
	}

	expr_fmt( buf, 1024, e );
	printf( "-->%s<--\n", buf );
	expr_free(e);

	(void) argc;
	(void) argv;
	return 0;
}

