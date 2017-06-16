/*
  purpose: demonstrate the reliability of advisory file locks to
  manage concurrent writing access to a file used as fifo queue
  Directory structure used to implement infinite file queue: 

  For each  fifo queue, one file system directory is used. The name
  of the directory is identical to the name of the fifo.
  The directory contains a sequence of files, which hold the contents of
  the fifo, and a file containing configuration information.
  Additional files are foreseenfor the read pointers for a set of independent
  readers.
  The contents files are named accorting to the scheme <Letter><Number>
  The Letter accords to the number of digits of the Number (A .. 1, B ..2 ...).
  The Numbers is in decimal digits. For each new generation of content file,
  the number is increased by one. The first file in the sequence is A0.
  ( A0, A1, .. , A9, B10, B11, .. , B99, C100, .. , C999, D1000 ....  ).
  The parameter file is named parameters.
  It contains the switch size for the file, and information about 
  record handling.
  If the current (contents file with highest number) contents file has a size
  above the switch size, a new generation of contents file is created empty.
  All further write requests go to the new generation.
  The record handling information consists of 2 bytes:
  1. Escape character (typically \), if blank, no record handling
  2. Record separator (typically \n).
  If record handling is in place, all escape characters and record separators
  in the user data are prefixed by the escape character before writing, and
  a record separator is appended. The read requests end at a record separator
  and revert remove the escape characters from the user data.
  If record handling is not in place, the data are written unmodified. Read
  operations use the read buffer size or read to the end of the fifo.

  Format of the parameter file: <Number><Blank><Escape><Separator>

  The openw operation returns a file write pointer, containing all relevant
  information for the following write operations.
  The structure is allocated dynamically, it should be released using the
  closew operation.

  The openr operation returns a file read pointer. 

  The write operation writes a record or a byte array to the open fifo.
  The read operation reads a record or a byte buffer from the open fifo.

  All operations are mutually exclusive. The switching of file generations
  is handled in the background transparent to the user.
  Read operations may be blocked indefinitely, if end of fifo is reached 
  and no new data are written to the fifo.
*/
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

static int dolock(int fd, int type) {

	int fres;
	struct flock flock;

	flock.l_whence = SEEK_SET;
	flock.l_start = 0;
	flock.l_len = 0;

	flock.l_type = type;
	fres = fcntl(fd, F_SETLKW, &flock);
	return fres;
}

static int takewritelock(int fd ) {
	return dolock(fd, F_WRLCK);
}

static int releaselock(int fd ) {
	return dolock(fd, F_UNLCK);
}

static int rolloverfile(const char* filename, int fd) {

	struct stat statbuf;
	ino_t inode1, inode2;
	int fd2;
	int res = -1;

	fd2 = open(filename, O_WRONLY);
	res = fstat(fd, &statbuf);
	if ( res < 0 ) goto RETURN;
	inode1 = statbuf.st_ino;
	res = fstat(fd2, &statbuf);
	if ( res < 0 ) goto RETURN;
	inode2 = statbuf.st_ino;
	
	if ( inode1 == inode2 ) {
		/* fd refers to the given filename, file is too big */
		close(fd2);
		res = rename(filename, "hallo.2");
		if ( res < 0 ) goto RETURN;
		fd2 = open(filename, O_WRONLY|O_CREAT|O_EXCL, 0666);
		if ( fd2 < 0 ) goto RETURN;
	} 
	/* just use new file */	
	takewritelock(fd2);
	close(fd);
	res = dup2(fd2, fd);

RETURN:
	return res;
}

#define MAXFILESIZE 1000

int writelocked( int fd, const char* buffer, size_t size ) {

	ssize_t wres = -1;
	off_t sres;
	int fres;

	fres = takewritelock(fd);
	if ( fres < 0 ) goto RETURN;
	sres = lseek(fd, 0, SEEK_END);

	if ( sres > MAXFILESIZE ) {
		rolloverfile("hallo", fd);
		sres = lseek(fd, 0, SEEK_END);
		if ( sres > MAXFILESIZE ) {
			goto RETURN;
		}
	}
	if ( sres < 0 ) goto RETURN;
	wres = write(fd, buffer, size);
	releaselock(fd);
RETURN:
	return wres;
}

#define BUFFER 20
int main( int argc, char * const* argv ) {

	int res = 1;
	char buffer[BUFFER+1];
	size_t size;
	ssize_t wres;
	int fd;

	if ( argc != 2 ) {
		fprintf(stderr, "usage: %s outputfile\n", argv[0]);
		goto RETURN;
	}

	fd = open(argv[1], O_WRONLY|O_CREAT, 0666);
	if ( fd < 0 ) {
		perror(argv[1]);
		goto RETURN;
	}

	while ( fgets(buffer, sizeof(buffer)-1, stdin) ) {
		size = strlen(buffer);
		wres = writelocked(fd, buffer, size);
		if ( (size_t) wres != size ) {
			if ( wres < 0 ) {
				perror("writelocked");
				break;
			}
			fprintf(stderr, "writelocked should write %lu bytes, but did %ld\n", size, wres);
		}
	}
	res = 0;
RETURN:
	exit(res);
}		
