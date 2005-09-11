
#define _GNU_SOURCE

#include <stdio.h>
#include <syslog.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

#include <opt.h>

/*
 * 2xx: ok
 * 201: stop
 * 202: pause
 * 203: play
 *
 * 4xx: temporary failures
 * 401: not yet implemented
 *
 * 5xx: errors:
 * 50x: command failure
 * 51x: open error
 *
 * 6xx: broadcast
 * 601: stop
 * 602: pause
 * 603: play
 * 62x: read error
 * 63x: decode error
 * 64x: output error
 *
 *
 */

/* seconds: */
#define PROGRES_INTERVAL 5
/* bytes: */
#define BUFLEN 4096

#define send_out(fmt...)	printf(fmt); fflush(stdout);

int input=-1, output=-1;
int paused = 0;
int elapsed = 0; /* msec */
int child = 0;

int got_sigalrm = 0;

// TODO: get exit status

static void open_output(char *command )
{
	int pfd[2];

	syslog( LOG_DEBUG, "forking >%s<", command );

	if( -1 == pipe(pfd) ){
		syslog( LOG_ERR, "pipe failed: %m" );
		exit( 1 );
	}

	if( -1 == (child = fork())){
		syslog( LOG_ERR, "fork failed: %m" );
		exit( 1 );

	} else if( child == 0 ){
		int fd;

		if( 0 > (fd = open("/dev/null", O_RDWR, 0700 ))){
			syslog( LOG_ERR, "open /dev/null: %m" );
			exit( 1 );
		}

		close(STDIN_FILENO);
		close(STDOUT_FILENO);
		close(STDERR_FILENO);

		dup2(pfd[0], STDIN_FILENO);
		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);

		if( fd != STDOUT_FILENO && fd != STDERR_FILENO )
			close( fd );

		close(pfd[1]);
		//close(pfd[0]);

		setsid();

		execl( command, command, NULL );
		syslog( LOG_ERR, "exec failed: %m" );

		exit(1);

	}

	syslog(LOG_DEBUG, "output child pid: %d", child );
	output = pfd[1];
	close(pfd[0]);
}

static void pause_output( void )
{
	if(child > 0 )
		kill(-child, SIGSTOP );
}

static void unpause_output( void )
{
	if(child > 0 )
		kill(-child, SIGCONT );
}

static void close_output( void )
{
	kill(-child, SIGTERM);
	kill(-child, SIGCONT);
	close(output);
	output = -1;
	child=0;
}

static void sig_alarm( int sig )
{
	++got_sigalrm;
	if( input >= 0 && ! paused )
		alarm(PROGRES_INTERVAL);
	signal(sig, sig_alarm);
}

static void do_status(int code)
{
	if( input < 0 ){
		send_out( "%d01 stop\n", code );
	} else if( paused ){
		send_out( "%d02 %d pause\n", code, elapsed );
	} else {
		send_out( "%d03 %d play\n", code, elapsed );
	}
}

static void do_pause( void )
{
	if( input >= 0 ){
		++paused;
		alarm(0);
		pause_output();
	}
	do_status(6);
}

static void do_unpause( void )
{
	paused = 0;
	if( input >= 0 ){
		alarm(PROGRES_INTERVAL);
		unpause_output();
	}
	do_status(6);
}

static void do_stop( void )
{
	if( input >= 0 )
		close(input);
	input = -1;

	close_output();

	alarm(0);
	elapsed = 0;
	paused = 0;
	do_status(6);
}

static int do_play( char *fname )
{
	if( input >= 0 )
		close(input);
	elapsed = 0;
	paused = 0;

	syslog(LOG_DEBUG, "playing >%s<", fname );

	if( -1 == ( input = open( fname, O_RDONLY))){
		syslog( LOG_ERR, "open failed: %m" );
		send_out( "510 %s\n", strerror(errno));
		alarm(0);
		return -1;
	} else {
		alarm(PROGRES_INTERVAL);
		unpause_output();
	}
	do_status(6);

	return 0;
}


static void do_seek( int offset )
{
	(void)offset;

	send_out("401 seek not yet implemented\n" ); /* TODO */
}

static void do_jump( int offset )
{
	(void)offset;

	send_out("401 jump not yet implemented\n" ); /* TODO */
}

static void handle_stdin( void )
{
	char *line = NULL;
	int lblen = 0;
	int len;

	if( -1 == (len = getline( &line, &lblen, stdin ))){
		syslog( LOG_ERR, "getline failed: %m" );
		exit( 1 );
	}
	line[--len] = 0; /* strip trailing newline */

	if( 0 == strncmp(line, "status", 6 )){
		do_status(2);
	} else if( 0 == strncmp(line, "play ", 5) ){
		if( -1 != do_play( line+5 )){
			send_out("200 ok\n");
		}
	} else if( 0 == strncmp(line, "stop", 4 ) ){
		do_stop();
		send_out("200 ok\n");
	} else if( 0 == strncmp(line, "pause", 5 )){
		do_pause();
		send_out("200 ok\n");
	} else if( 0 == strncmp(line, "unpause", 7 )){
		do_unpause();
		send_out("200 ok\n");
	} else if( 0 == strncmp(line, "seek ", 5 )){
		int offset = atoi(line+5);
		do_seek(offset);
		send_out("200 ok\n");
	} else if( 0 == strncmp(line, "jump ", 5 )){
		int offset = atoi(line+5);
		do_jump(offset);
		send_out("200 ok\n");
	} else {
		send_out("500 unknown command\n" );

	}

	free(line);
}

#define XFDSET(fd,set,max) FD_SET( fd, set ); if( max < fd ) max = fd;
int main( int argc, char **argv )
{
	char *command;
	char buf[BUFLEN];
	int bufed = 0;

	openlog( "dudlw", LOG_PID | LOG_PERROR, LOG_DAEMON );

	if( argc == 2 ){
		command = argv[1];
	} else {
		command = opt_player;
	}

	signal(SIGALRM, sig_alarm );
	signal(SIGCHLD, SIG_IGN );
	signal(SIGPIPE, SIG_IGN );

	setlinebuf(stdin);

	while(1){
		int maxfd = 0;
		fd_set fdread, fdwrite;

		/* deal with signals that interrupted select() */

		if( got_sigalrm ){
			do_status(6);
			got_sigalrm = 0;
		}

		/* try to (re-)open output program */
		if( output < 0){
			open_output(command);
		}
		/* fill fd-sets for select */
		FD_ZERO( &fdread );
		FD_ZERO( &fdwrite );
		XFDSET( fileno(stdin), &fdread, maxfd );
		if( input >= 0 ){
			if( bufed <= 0 ){
				XFDSET( input, &fdread, maxfd );
			}

			if( ! paused ){
				XFDSET( output, &fdwrite, maxfd );
			}
		}

		maxfd++;

		if( 0 > select( maxfd, &fdread, &fdwrite, NULL, NULL )){
			if( errno != EINTR ){
				syslog( LOG_CRIT, "select failed: %m" );
				exit( 1 );
			}

			/* 
			 * we got a signal - maybe sigchild. 
			 * as fdsets are invalid anyways, we restart again
			 */
			continue;
		}

		/* fill buffer */
		if( bufed <= 0 && input >= 0 && FD_ISSET(input, &fdread) ){
			if( -1 == (bufed = read(input, buf, BUFLEN))){
				syslog(LOG_ERR, "reading failed: %m");
				send_out("620 %s\n", strerror(errno));
				do_stop();

			} else if( bufed == 0 ){ /* eof */
				do_stop();

			}

		}

		/* flush buffer */
		if( ! paused && bufed > 0 && FD_ISSET(output, &fdwrite ) ){
			int written;

			if( -1 == (written = write(output,buf,bufed))){
				syslog(LOG_ERR, "writing failed: %m" );
				send_out("640 %s\n", strerror(errno));
				close_output();
				do_pause();

			} else if( written != bufed ){
				syslog(LOG_ERR, "write failed: partial buffer written" );
				send_out("641 partial buffer written\n");
				close_output();
				do_pause();

			} else {
				elapsed += bufed; /* TODO: elapsed = time */
				bufed = 0;
			}
		}

		if( FD_ISSET(fileno(stdin), &fdread ) ){
			handle_stdin();
		}

	}

	return 0;
}