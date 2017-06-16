
#define	_POSIX_SOURCE
#define _POSIX_C_SOURCE 199506

#include 	<time.h>
#include 	<sys/time.h>
#include	<stdio.h>
#include	<sys/stat.h>
#include	<fcntl.h>
#include	<unistd.h>
#include	<limits.h>
#include	<string.h>
#include	<dirent.h>
#include	<stdlib.h>
#include	<errno.h>

#include	"fifo.h"

char fifoERROR[10240];

int main(int argc, char * const* argv) {

	int res;
	ssize_t wres, rres;
	FifoDescriptor* fwd;
	FifoDescriptor* frd;
	char* filename;
	char buffer[10000];
	size_t size;
	FILE* fp = NULL;

	if ( argc < 3 || strlen(argv[1]) > 2 ) {
		fprintf(stderr, "usage: %s r|w file [input]\n", argv[0]);
		exit(1);
	}
	if ( argc >= 4 ) {
		fp = fopen(argv[3], "r");
	}
	if ( fp == NULL ) {
		fp = stdin;
	}

	filename = argv[2];

	res = fifoCreate(filename, 1000, '\\', '\n');
	if ( res < 0 ) {
		perror("fifoCreate failed");
		fprintf(stderr, fifoERROR);
		goto RETURN;
	}

	if ( strchr(argv[1], 'w') ) {
		fwd = fifoOpenW(filename);
		if ( fwd == NULL ) {
			perror("fifoOpenW failed");
			fprintf(stderr, fifoERROR);
			goto RETURN;
		}

		
		while ( fgets(buffer, sizeof(buffer)-1, fp) ) {
			size = strlen(buffer)-1;
			buffer[size] = '\0';
			wres = fifoWrite(fwd, buffer, size);
			if ( wres < 0 ) {
				perror("fifoWrite failed: ");
				fprintf(stderr, fifoERROR);
			}
		}
		fifoCloseW(fwd);
	}


	if ( strchr(argv[1], 'r') ) {
		frd= fifoOpenR(filename, "0000");
		if ( frd == NULL ) {
			perror("fifoOpenR failed");
			fprintf(stderr, fifoERROR);
			goto RETURN;
		}

		while ( (rres = fifoReadW(frd, buffer, sizeof(buffer), 100, 10000)) >= 0 ) {
			printf("%.*s\n", (int) rres, buffer);
			fflush(stdin);
			rres = fifoRelease(frd);
		}
		if ( rres < 0 ) {
			perror("fifoRead failed");
			fprintf(stderr, fifoERROR);
			goto RETURN;
		}

		fifoCloseR(frd);
	}
RETURN:
	exit(0);
}

