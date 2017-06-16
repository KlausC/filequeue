/*
  purpose: demonstrate the reliability of advisory file locks to
  manage concurretn writing access to a file used as fifo queue
*/
#define _POSIX_C_SOURCE 199309L
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static int poffset( char* rbuffer, off_t o1, off_t o2 ) {
	return sprintf(rbuffer, "%022ld %022ld\n", o1, o2);
}

static int readoffset(int fdadm, off_t* o1, off_t* o2 ) {
	char rbuffer[50];
	size_t rres;
	int res = 0;

	*o1 = 0;
	*o2 = 0;
	rres = read(fdadm, rbuffer, sizeof(rbuffer));
	if ( rres <= 0 ) goto RETURN;
	rbuffer[rres] = '\0';
	sscanf( rbuffer, "%ld%ld", o1, o2);
RETURN:
	return res;
}

static int printoffset(int fdadm, off_t o1, off_t o2) {
	ssize_t wres;
	off_t sres;
	char rbuffer[50];
	int res = -1;

	poffset(rbuffer, o1, o2);
	sres = lseek(fdadm, 0, SEEK_SET);
	if ( sres < 0 ) goto RETURN;
	wres = write(fdadm, rbuffer, strlen(rbuffer));
	if ( wres < 0 ) goto RETURN;
	res = 0;
RETURN:
	return res;
}

int readlocked(int fd, int fdadm, char* buffer, size_t size, off_t* off) {

	ssize_t wres = -1;
	long i;
	off_t sres;
	off_t offset;
	off_t newoffset;
	int fres;
	struct flock flock;
	char rbuffer[25];
	char* endnum;
	int res;

	flock.l_whence = SEEK_SET;
	flock.l_start = 0;
	flock.l_len = 0;

	flock.l_type = F_WRLCK;
	fres = fcntl(fdadm, F_SETLKW, &flock);
	if ( fres < 0 ) goto RETURN;
	flock.l_type = F_RDLCK;
	fres = fcntl(fd, F_SETLKW, &flock);
	if ( fres < 0 ) goto RETURN;
	sres = lseek(fdadm, 0, SEEK_SET);
	if ( sres < 0 ) goto RETURN;
	res = readoffset(fdadm, &offset, &newoffset);
	if ( res < 0 ) goto RETURN;
	if ( newoffset != 0 ) {
		wres = 0; 
		goto RETURN;	/* must first call release */
	}
	sres = lseek(fd, offset, SEEK_SET);
	if ( sres < 0 ) {
		fprintf(stderr, "lseek error: %s\n", rbuffer );
		wres = -1;
		goto RETURN;
	}
	wres = read(fd, buffer, size);
	if ( wres <= 0 ) goto RETURN;

	endnum = NULL;
	for ( i = 0; i < wres; ++i ) {
		if ( buffer[i] == '\n' ) {
			endnum = buffer + i;
			break;
		}
	}
	if ( endnum == NULL ) {
		endnum = buffer + wres;
	} else {
		wres = (endnum - buffer) + 1;
	}
	*off = offset;
	offset += wres;
RETURN:
	flock.l_type = F_UNLCK;
	fcntl(fd, F_SETLKW, &flock);

	if ( wres > 0 ) {
		printoffset(fdadm, *off, offset);
	}

	flock.l_type = F_UNLCK;
	fcntl(fdadm, F_SETLKW, &flock);
	return wres;
}

int release( int fdadm, off_t reloff, size_t size ) {

	ssize_t wres = -1;
	off_t sres;
	off_t offset;
	off_t newoffset;
	int fres;
	int res;
	struct flock flock;

	flock.l_whence = SEEK_SET;
	flock.l_start = 0;
	flock.l_len = 0;

	flock.l_type = F_WRLCK;
	fres = fcntl(fdadm, F_SETLKW, &flock);
	if ( fres < 0 ) goto RETURN;
	sres = lseek(fdadm, 0, SEEK_SET);
	if ( sres < 0 ) goto RETURN;
	res = readoffset(fdadm, &offset, &newoffset);
	if ( res < 0 ) goto RETURN;
	if ( reloff != offset || reloff + (off_t) size != newoffset ) {
		wres = -1;
		goto RETURN;
	}
	printoffset(fdadm, newoffset, 0L);
RETURN:
	flock.l_type = F_UNLCK;
	fcntl(fdadm, F_SETLKW, &flock);
	return wres;
}

int readlockedw(int fd, int fdadm, char* buffer, size_t size, long wtim, off_t* off) {
	ssize_t rres;
	struct timespec interval;

	interval.tv_sec = wtim / 1000;
	interval.tv_nsec = (wtim * 1000000) % 1000000000;

	while ( (rres = readlocked(fd, fdadm, buffer, size, off)) == 0 ) {
		nanosleep(&interval, NULL);
	}
	return rres;
}

#define BUFFER 1024
int main( int argc, char * const* argv ) {

	int res = 1;
	char buffer[BUFFER+1];
	ssize_t wres;
	int fd;
	int fdadm;
	off_t off;	

	if ( argc != 3 ) {
		fprintf(stderr, "usage: %s inputfile adminfile\n", argv[0]);
		goto RETURN;
	}

	fd = open(argv[1], O_RDONLY, 0666);
	if ( fd < 0 ) {
		perror(argv[1]);
		goto RETURN;
	}

	fdadm= open(argv[2], O_RDWR|O_CREAT, 0666);
	if ( fd < 0 ) {
		perror(argv[2]);
		goto RETURN;
	}

	while ( (wres = readlockedw(fd, fdadm, buffer, sizeof(buffer), 500, &off)) > 0 ) {
		write(1, buffer, wres);
		release(fdadm, off, wres);
	}
	res = 0;
RETURN:
	exit(res);
}		
