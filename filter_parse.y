
/************************************************************
 * C declarations
 */
%{
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>

#include "filter_defs.h"

#define YYDEBUG 1
#define YYERROR_VERBOSE

#define YYPARSE_PARAM parm
#define YYLEX_PARAM parm
#define PARM	((struct parse_buf *)parm)

union parse_val {
	char *string;
	t_bool *exp;
};

#define YYSTYPE union parse_val

static int yylex( YYSTYPE *lvalp, /* YYLTYPE *llocp,*/  void *parm );
static void yyerror( const char *s );
%}

/************************************************************
 * BISON declarations
 */
%defines
%pure_parser
%debug

%token <string> STRING
%token <string> NAME

%%
/************************************************************
 * Grammar 
 */

input:	  /* empty */
	| expr { ((struct parse_buf*)parm)->res = $<exp>$ = $<exp>1; }
;

expr:	
	  expr '|' expr			{ 
		$<exp>$ = bool_or( $<exp>1, $<exp>3 ); 
		}
	| expr '&' expr			{ 
		$<exp>$ = bool_and( $<exp>1, $<exp>3 ); 
		}
	| '!' expr			{ 
		$<exp>$ = bool_not( $<exp>2 ); 
		}
	| '(' expr ')'			{ $<exp>$ = $<exp>2; } 
	| NAME '=' STRING		{ 
		$<exp>$ = bool_check( $1, "=", $3, NULL );
		free($1);
		free($3);
		if( NULL == $<exp>$ ){
			YYABORT;
		}
		} 
;


%%
/************************************************************ 
 * additional C Code
 */

static void yyerror( const char *s )
{
	fprintf( stderr, "%s\n", s );
}

static int buf_next( struct parse_buf *p )
{
	if( p->pos >= 0 && p->str[p->pos] == 0 ){
		return EOF;
	}

	return p->str[p->pos++];
}

static int buf_prev( struct parse_buf *p )
{
	if( p->pos <= 0 ){
		return EOF;
	}

	return p->str[--p->pos];
}

static int lex_name( YYSTYPE *lvalp, struct parse_buf *p )
{
	int c;
	char *name = NULL;
	int used = 0;
	int avail = 0;

	while( isalnum( c = buf_next(p) ) ){
		if( used >= avail ){
			char *new;
			avail += 100;
			if( NULL == (new = realloc(name, avail+1))){
				free(name);
				return 0; /* TODO: proper value */
			}
			name = new;
		}
		name[used++] = c;
	}
	if( name )
		name[used] = 0;

	lvalp->string = name;
	return NAME;
}

static int lex_string( YYSTYPE *lvalp, struct parse_buf *p )
{
	int c;
	char *name = NULL;
	int used = 0;
	int avail = 0;

	while( EOF != (c = buf_next(p) )){
		if( c == '"' ){
			break;

		} else if( c == '\\' ){
			int d = buf_next(p);
			switch( d ){
			  /* no next char - keep the old one */
			  case EOF:
			  	break;

			  /* otherwise use the escaped one */
			  default:
				c = d;
				break;
			}
		}

		if( used >= avail ){
			char *new;
			avail += 100;
			if( NULL == (new = realloc(name, avail+1))){
				free(name);
				return 0; /* TODO: proper value */
			}
			name = new;
		}
		name[used++] = c;
	}
	if( name )
		name[used] = 0;

	lvalp->string = name;
	return STRING;
}

static int yylex( YYSTYPE *lvalp, /* YYLTYPE *llocp,*/  void *parm )
{
	struct parse_buf *p = (struct parse_buf *)parm;
	int c;

	while( isspace( c = buf_next(p) ));

	if( c == EOF ){
		return 0;
	}

	/* string */
	if( c == '"' ){
		return lex_string(lvalp, p);
	}

#if 0
	/* number */
	if( c == '.' || isdigit(c) ){
		buf_prev(p);
		return lex_num(lvalp, p);
	}
#endif

	/* name */
	if( isalpha(c) ){
		buf_prev(p);
		return lex_name(lvalp, p);
	}

	/* other char */
	return c;
}


