
#include <stdio.h>

#include "filter_defs.h"
//#include "filter_parse.h"

//#define FILT "genre IN ( 1,3,4 ) & ! genre IN( 5,6,7)"
#define FILT "genres f \"huhu\" & artist = \"faf\" | ! title = \"fff\""

extern int yyparse( void *);
extern int yydebug;

int main( int argc, char **argv )
{
	char buf[1024];
	struct parse_buf pars;

	pars.res = NULL;
	pars.str = FILT;
	pars.pos = 0;

	yydebug = 1;

	if( yyparse( (void*) &pars )){
		return 1;
	}
#if 0
	t_bool *b, *b1, *b2, *n, *a1, *a2;

	if( NULL == (b = bool_check( "genres", "IN", "a", "b", NULL )))
		return 1;
	if( NULL == (n = bool_not( b )))
		return 1;

	if( NULL == (b1 = bool_check( "genres", "=", "g", NULL )))
		return 1;
	if( NULL == (a1 = bool_and( b1, n )))
		return 1;

	if( NULL == (b2 = bool_check( "artist", "=", "gfdgd", NULL )))
		return 1;
	if( NULL == (a2 = bool_and( b2, a1 )))
		return 1;

#endif
	if( pars.res ){
		if( 1024 < bool_fmt( buf, 1024, pars.res ))
			return 1;
		printf( "%s\n", buf );

		bool_done( pars.res );
	}

	(void)argc;
	(void)argv;

	return 0;
}
