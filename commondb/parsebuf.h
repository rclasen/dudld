
#ifndef _COMMONDB_PARSEBUF_H
#define _COMMONDB_PARSEBUF_H

/************************************************************
 *
 * abstraction of an input buffer for the parser.
 *
 * - for input files or strings
 * - allows to go back in input (even in non-seekable files)
 *
 * theory of operation:
 * - fetch next char(s) with PI_NEXT. On a non-seekable input this may
 *   buffer the chars to be able to go back.
 * - if you are done with the input, free the buffer up to where you've
 *   read with PI_DONE.
 * - if you didn't like the input and want to read it differently call
 *   PI_UNDO and start again.
 */

/*
 * the internal handle - don't use yourself!!!
 */
typedef struct {
	int (*next)( void *data );
	int (*eof)( void *data );
	int (*line)( void *data );
	int (*col)( void *data );
	void (*done)( void *data );
	void (*undo)( void *data );
	void (*free)( void *data );
	void *data;
} parser_input;




/************************************************************
 *
 * public interface:
 */

#define PINPUT(obj,metd)	(*(obj)->metd)

/* fetch a single char */
#define PI_NEXT(i)	PINPUT(i,next)( (i)->data)

/* did last operation reach end of input? */
#define PI_EOF(i)	PINPUT(i,eof)( (i)->data)

/* tell currently parsed line (starting with 0) */
#define PI_LINE(i)	PINPUT(i,line)( (i)->data)

/* tell currently parsed Column (starting with 0) */
#define PI_COL(i)	PINPUT(i,col)( (i)->data)

/* dispose read input and move "point of no return" */
#define PI_DONE(i)	PINPUT(i,done)( (i)->data)

/* seek back to "point of no return" - ie where PI_DONE was called last */
#define PI_UNDO(i)	PINPUT(i,undo)( (i)->data)

/* open a new string buffer */
parser_input *pi_str_new( const char *in );

/* dispose a puffer */
void pi_free( parser_input *pi );



#endif
