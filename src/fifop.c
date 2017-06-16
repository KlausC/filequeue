
#define _POSIX_C_SOURCE 200112L

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
#include	<pthread.h>

#include	"fifo.h"

/* static functions ahead declarations */
static int fifoWriteParams(const char* dirname, const FifoParameters* fpa );
static int fifoReadParams(const char* dirname, FifoParameters* fpa );
static char* fifoAbsfilename( const char* filename );
static long fifoGetCurrent(const char* filename);
static char* fifoCurrentAbsfilename(const char* dirname, unsigned long current);
static char* fifoFormatWriteBuffer(FifoParameters*, const char* buff, size_t*);
static ssize_t writelocked(FifoDescriptor*, const char* buffer, size_t);
static void err( const char* text );
static ssize_t fifoFormatReadBuffer(FifoParameters *fp, char* buffer, ssize_t*);
static ssize_t readlocked(FifoDescriptor*, char* buffer, size_t);
static ssize_t release(FifoDescriptor* frd);
static int fifoOpenFilePointer(FifoDescriptor* frd, const char* readpf);
static int fifoReadFilePointer(FifoDescriptor* frd);
static int fifoWriteFilePointer(FifoDescriptor* frd);
static int fifoReOpenRead(FifoDescriptor* frd);
static int takewritelock(int fd);
static int takereadlock(int fd);
static int releaselock(int fd);
static int lwlock(pthread_rwlock_t*);
static int lrlock(pthread_rwlock_t*);
static int lulock(pthread_rwlock_t*);

/* process internal (pthread) locks */
static pthread_rwlock_t lockData = PTHREAD_RWLOCK_INITIALIZER;
static pthread_rwlock_t lockWadm = PTHREAD_RWLOCK_INITIALIZER;
static pthread_rwlock_t lockRadm = PTHREAD_RWLOCK_INITIALIZER;

/* fixed size buffer for internal error messages */
char fifoERROR[10240];

/**
 * Create (if no directory with given name exists) a new file queu structure.
 * It consists of a base directory, containing configuration and metadata files and
 * a flexible number of data files.
 * The files are in detail:
 * - dir/.param contains static parameters of the file queue
 *              - rollover data size
 *              - escape character
 *              - message separator character
 * - dir/.wp write pointer, contains current file number for writing
 * - dir/.pr_xxxx one of several possible read pointers contains
 *   			- file number for reading using this pointer
 *   			- position to read next message
 *
 *  Data files
 * - dir/A0 .. A9 B10.. B99 C100 .. C999 D1000 .. D9999 
 *
 */
int fifoCreate( const char* dirname, off_t switchSize, char esc, char sep ) {
	int res;
	FifoParameters fpa;
	fpa.pathName = NULL;
	fpa.switchSize = switchSize;
	fpa.escape[0] = esc;
	fpa.separator[0] = sep;
	
	err(NULL);
	res = mkdir(dirname, 0777);
	if ( res < 0 && errno != EEXIST ) {
		err("fifoCreate mkdir:");
		goto RETURN;
	}
	if ( res < 0 ) {
		/* directory existed already */
		res = fifoReadParams(dirname, &fpa);
		if ( res < 0 ) {
			err(NULL);
			res = fifoWriteParams(dirname, &fpa);
		}	
	} else {
		/* directory was just created */
		res = fifoWriteParams(dirname, &fpa);
	}
RETURN:
	return res;
}

/**
 * Open file for writing.
 * Take write locks during change of the write pointer file.
 * The write pointer structure must be used in the same thread, which opened it.
 * Return write pointer.
 */
FifoDescriptor* fifoOpenW( const char* filename ) {

	char* name;
	int res = -1;
	long resl;
	int lres = -1;
	int fres = -1;
	FifoDescriptor* fwd;
	FifoDescriptor* fp = NULL;

	err(NULL);
	fwd = (FifoDescriptor*) malloc(sizeof(*fwd));
	if ( fwd == NULL ) {
		err("fifoOpenW malloc descriptor:");
		goto RETURN;
	}

	fwd->fdp = -1;
	fwd->fd = -1;

	fwd->parameters = (FifoParameters*) malloc(sizeof(*fwd->parameters));
	if ( fwd->parameters == NULL ) {
		err("fifoOpenW malloc parameters:");
		goto RETURN;
	}

	fwd->filePointer = (FifoFilePointer*) malloc(sizeof(*fwd->filePointer));
	if ( fwd->filePointer == NULL ) {
		err("fifoOpenR malloc filePointer:");
		goto RETURN;
	}

	fwd->parameters->pathName = fifoAbsfilename(filename);
	if ( fwd->parameters->pathName == NULL ) {
		err("fifoOpenW fifoAbsfilename:");
		goto RETURN;
	}

	res = fifoReadParams(filename, fwd->parameters);
	if ( res < 0 ) {
		err("fifoOpenW read parameters:");
		goto RETURN;
	}
	
	res = fifoOpenFilePointer(fwd, NULL);
	if ( res < 0 ) {
		err("fifoOpenR open read pointer:");
		goto RETURN;
	}

	lres = lwlock(&lockWadm);
	fres = takewritelock(fwd->fdp);
	if ( lres < 0 || fres < 0 ) {
		err("fifoOpenW:");
		goto RETURN;
	}
	res = fifoReadFilePointer(fwd); /* the write pointer */
	if ( fwd->filePointer->current <= 0 ) {
		resl = fifoGetCurrent(fwd->parameters->pathName);
		fwd->filePointer->current = resl;
	}

	fwd->current = fwd->filePointer->current;
	if ( resl < 0 ) {
		err("fifoOpenW get current:");
		goto RETURN;
	}

	name = fifoCurrentAbsfilename(filename, fwd->current);
	if ( name == NULL ) {
		err("fifoOpenW fifoCurrentAbsfilename:");
		goto RETURN;
	}

	fwd->fd = open(name ,O_WRONLY | O_CREAT, 0666);
	free(name);
	if ( fwd->fd < 0 ) {
		err("fifoOpenW open ");
		err(name);
		err(":");
		goto RETURN;
	}
	
	fifoWriteFilePointer(fwd);
	fp = fwd;
RETURN:
	if ( fres >= 0 ) releaselock(fwd->fdp);
	if ( lres >= 0 ) lulock(&lockWadm);
	if ( fp == NULL ) {
		if ( fwd && fwd->parameters ) {
			if ( fwd->parameters->pathName ) {
				free(fwd->parameters->pathName);
			}
			free(fwd->parameters);
			free(fwd);
		}
	}
	return fp;
}

/**
 * Open read stream for the file queue. Multiple read streams, identified by
 * a unique name, can operate on the same file queue.
 * Take a read lock on the read pointer file during operation.
 * The returned read pointer has to be used for all read and release operations
 * for this read stream.
 * Return read pointer.
 */
FifoDescriptor* fifoOpenR( const char* filename, const char* readpf) {

	char* name;
	int res = -1;
	int lres = -1;
	int fres = -1;
	FifoDescriptor* frd;
	FifoDescriptor* fp = NULL;

	err(NULL);
	frd = (FifoDescriptor*) malloc(sizeof(*frd));
	if ( frd == NULL ) {
		err("fifoOpenR malloc descriptor:");
		goto RETURN;
	}

	frd->parameters = (FifoParameters*) malloc(sizeof(*frd->parameters));
	if ( frd->parameters == NULL ) {
		err("fifoOpenR malloc parameters:");
		goto RETURN;
	}

	frd->fd = -1;
	frd->fdp = -1;

	frd->filePointer = (FifoFilePointer*) malloc(sizeof(*frd->filePointer));
	if ( frd->filePointer == NULL ) {
		err("fifoOpenR malloc filePointer:");
		goto RETURN;
	}

	frd->parameters->pathName = fifoAbsfilename(filename);
	if ( frd->parameters->pathName == NULL ) {
		err("fifoOpenR fifoAbsfilename:");
		goto RETURN;
	}

	res = fifoReadParams(filename, frd->parameters);
	if ( res < 0 ) {
		err("fifoOpenR read parameters:");
		goto RETURN;
	}
	
	res = fifoOpenFilePointer(frd, readpf);
	if ( res < 0 ) {
		err("fifoOpenR open read pointer:");
		goto RETURN;
	}
	
	lres = lrlock(&lockRadm);
	fres = takereadlock(frd->fdp);
	if ( lres < 0 || fres < 0 ) {
		err("fifoOpenRead:");
		goto RETURN;
	}
	res = fifoReadFilePointer(frd);

	name = fifoCurrentAbsfilename(filename, frd->filePointer->current);
	if ( name == NULL ) {
		err("fifoOpenR fifoCurrentAbsfilename:");
		goto RETURN;
	}

	frd->fd = open(name, O_RDONLY|O_CREAT, 0666);
	free(name);
	if ( frd->fd < 0 ) {
		err("fifoOpenR open ");
		err(name);
		err(":");
		goto RETURN;
	}
	frd->current = frd->filePointer->current;
	fp = frd;
RETURN:
	if ( fres >= 0 ) releaselock(frd->fdp);
	if ( lres >= 0 ) lulock(&lockRadm);
	if ( fp == NULL ) {
		if ( frd ) {
			if ( frd->parameters ) {
				if ( frd->parameters->pathName ) {
					free(frd->parameters->pathName);
				}
				free(frd->parameters);
			}
			if ( frd->filePointer ) {
				if ( frd->filePointer->readPointerFile ) {
					free(frd->filePointer->readPointerFile);
				}
				free(frd->filePointer);
			}
		free(frd);
		}
	}
	return fp;
}

/**
 * Write a message to the file queue.
 * Format the message and write to current data file.
 */
ssize_t fifoWrite( FifoDescriptor* fwd, void* buffer, size_t size ) {

	char* newbuffer;
	ssize_t res = -1;
	size_t siz = size;

	err(NULL);
	newbuffer = fifoFormatWriteBuffer(fwd->parameters, buffer, &siz);
	if ( newbuffer == NULL ) {
		err("fifoWrite:");
		goto RETURN;
	}

	res = writelocked(fwd, newbuffer, siz);

RETURN:
	if ( newbuffer && newbuffer != buffer ) free(newbuffer);
	return res;

}

/**
 * Read a message from open read stream of file queue.
 * The message must fit into the provided buffer.
 * Undo the message formatting done during write.
 */
ssize_t fifoRead( FifoDescriptor* frd, void* buffer, size_t size ) {
	ssize_t res;
	err(NULL);
	res = readlocked(frd, buffer, size);
	return res;
}

/**
 * Release the previously read message from the open read stream.
 * If no unreleased message exists, silently ignore this call.
 * Note: only data files, which do not contain unreleased messages by any
 * read pointer may be removed from file system.
 */
ssize_t fifoRelease(FifoDescriptor* frd) {
	err(NULL);
	return release(frd);
}

/**
 * Close read pointer.
 */
void fifoCloseR( FifoDescriptor* fp ) {
	err(NULL);
	if ( fp == NULL ) return;
	if ( fp->fd >= 0 ) close(fp->fd);
	if ( fp->parameters ) {
		if ( fp->parameters->pathName ) {
			free(fp->parameters->pathName);
		}
		free(fp->parameters);
	}
	free(fp);
}

/**
 * Close write pointer.
 */
void fifoCloseW( FifoDescriptor* fp ) {
	err(NULL);
	if ( fp == NULL ) return;
	if ( fp->fd >= 0 ) close(fp->fd);
	if ( fp->parameters ) {
		if ( fp->parameters->pathName ) {
			free(fp->parameters->pathName);
		}
		free(fp->parameters);
	}
	free(fp);
}

/*************** END OF PUBLIC INTERFACE *************************************/

/**
 * Construct parameters file name in malloced space.
 */
static char* fifoParamsFilename(const char* dirname) {
	int namelen;
	char* name;
	const char PARFILE[] = ".param";

	namelen = strlen(dirname) + 2 + strlen(PARFILE);
	name = (char*) malloc(namelen);
	if ( name == NULL ) {
		err("fifoParamsFilename malloc:");
		goto RETURN;
	}
	strcpy(name, dirname);
	strcat(name, "/");
	strcat(name, PARFILE);
RETURN:
	return name;
}

/**
 * Write parameters file.
 */
static int fifoWriteParams(const char* dirname, const FifoParameters* fpa ) {
	int fd;
	int res = -1;
	int lres = -1;
	int fres = -1;
	char* name;
	char buffer[50];
	ssize_t wres;
	long len;

	name = fifoParamsFilename(dirname);
	if ( name == NULL ) {
		err("fifoWriteParams:");
		goto RETURN;
	}

	fd = open(name, O_WRONLY | O_CREAT, 0666);
	if ( fd < 0 ) {
		err("fifoWriteParams openw ");
		err(name);
		err(":");
		goto RETURN;
	}

	lres = lwlock(&lockWadm);
	fres = takewritelock(fd);
	if ( lres < 0 || fres < 0 ) {
		err("fifoWriteParams:");
		goto RETURN;
	}
	sprintf(buffer, "%ld ..\n", (long)fpa->switchSize);
	len = strlen(buffer);
	buffer[len-3] = fpa->escape[0];
	buffer[len-2] = fpa->separator[0];
	wres = write(fd, buffer, strlen(buffer));
	if ( wres < 0 ) {
		err("fifoWriteParams write:");
		goto RETURN;
	}
	res = 0;
RETURN:
	if ( fres >= 0 ) releaselock(fd);
	if ( lres >= 0 ) lulock(&lockWadm);
	if ( fd >= 0 ) {
		close(fd);
	}
	if ( name ) free(name);
	return res;
}

/**
 * Read parameters file.
 */
static int fifoReadParams(const char* dirname, FifoParameters* fpa ) {
	int fd;
	int res = -1;
	int lres = -1;
	int fres = -1;
	char* name;
	char buffer[50];
	ssize_t rres;
	long len;

	fpa->switchSize = 0L;
	fpa->escape[0] = ' ';
	fpa->separator[0] = ' ';

	name = fifoParamsFilename(dirname);
	if ( name == NULL ) {
		err("fifoReadParams:");
		goto RETURN;
	}

	fd = open(name, O_RDONLY);
	if ( fd < 0 ) {
		err("fifoReadParams open ");
		err(name);
		err(":");
		goto RETURN;
	}

	lres = lrlock(&lockWadm);
	fres = takereadlock(fd);
	if ( lres < 0 || fres < 0 ) {
		err("fifoReadParams:");
		goto RETURN;
	}
	rres = read(fd, buffer, sizeof(buffer)-1);
	if ( rres < 0 ) {
		err("fifoReadParams read:");
		goto RETURN;
	}
	buffer[rres] = 0;
	sscanf(buffer, "%ld ", &fpa->switchSize);
	len = strlen(buffer);
	fpa->escape[0] = buffer[len-3];
	fpa->separator[0] = buffer[len-2];
	fpa->rollmark[0] = fpa->escape[0];
	fpa->rollmark[1] = '@';
	fpa->rollmark[2] = fpa->separator[0];
	fpa->rollmark[3] = '\0';
	res = 0;
RETURN:
	if ( fres >= 0 ) releaselock(fd);
	if ( lres >= 0 ) lulock(&lockWadm);
	if ( fd >= 0 ) {
		close(fd);
	}
	if ( name ) free(name);
	return res;
}

/**
 * Calculate absolute file name in malloced space
 */
static char* fifoAbsfilename( const char* filename ) {
	char* dname = NULL;
	char* name = NULL;
	
	if ( filename[0] != '/' ) {
		dname = (char*) malloc(_POSIX_PATH_MAX+1);	
		if ( dname == NULL ) goto RETURN;
		if ( getcwd(dname, _POSIX_PATH_MAX+1) == NULL ) goto RETURN;
		name = (char*) malloc(strlen(dname) + strlen(filename) + 2);
		if ( name == NULL ) goto RETURN;
		strcpy(name, dname);
		strcat(name, "/");
		strcat(name, filename);
	} else {
		name = (char*) malloc(strlen(filename) + 1);
		if ( name == NULL ) goto RETURN;
		strcpy(name, filename);
	}
RETURN:
	if ( dname != NULL ) free(dname);
	return name;
}

/**
 * Calculate number of decimal digits of data file name given first letter
 * of name ( A => 1, B => 2 ...
 */
static size_t fifoNamelen(int c) {
	return c - 'A' + 1;
}

/**
 * Put first character of data file name according to length of name.
 */
static char* fifoFilename(unsigned long current, char* name) {
	sprintf(name+1, "%lu", current );
	name[0] = 'A' + strlen(name+1) - 1;
	return name;
}

/**
 * Calculate absolute file name from file number of data file in malloced space.
 */
static char* fifoCurrentAbsfilename(const char* dirname, unsigned long current) {
	char* name = (char*) malloc(strlen(dirname) + 26 );
	if ( name == NULL) goto RETURN;
	strcpy(name, dirname);
	strcat(name, "/");
	fifoFilename(current, name+strlen(name));
RETURN:
	return name;
}

/**
 * Find newest (highest numerical value of name) file in file queue directory
 * and return this maximum value as a long integer. If no files accoring to the naming
 * scheme are contained in the directory listing, return 0L.
 * The file naming scheme the file number (0 ... MAX_LONG) is converted in a decimal 
 * string. The string size (between 1 and 20) determines the start character
 * 1 => A, 2 => B, ... 26 => Z from the ASCII upercase letters.
 */
static long fifoGetCurrent(const char* filename) {

	DIR* dir;
	unsigned long res;
	unsigned long max = -1;
	struct dirent* dirent;
	char *cp;

	dir = opendir(filename);
	if ( dir == NULL ) {
		err("fifoGetCurrent opendir ");
		err(filename);
		err(":");
		goto RETURN;
	}

	max = 0;
	while ( (dirent = readdir(dir)) ) {
		if ( fifoNamelen(dirent->d_name[0])+1 == strlen(dirent->d_name)) {
			res = strtoul(dirent->d_name+1, &cp, 10);
			if ( res == ULONG_MAX ) continue;
			if ( *cp ) continue;
			if ( res > max ) {
				max = res;
			}
		}
	}

RETURN:
	if ( dir ) closedir(dir);
	return max;
}

/**
 * If the escape char is blank, return the address of the input buffer.
 * Otherwise count the number of characters to be escaped and allocate a new
 * output buffer. Prepend each occurence of escape and separator character by an
 * additional escape character. Add a separator character and a NUL character
 * to the end of the new buffer. Store the actual size in the size variable and
 * return the address of the new buffer.
 * Warning: new buffer addres has to be released by the consumer.
 */
static char* fifoFormatWriteBuffer(FifoParameters *fp, const char* buffer, size_t *size) {

	int res = 0;
	char* newbuffer;
	char ch;
	size_t siz = *size;
	size_t i;
	size_t j;

	if ( fp->escape[0] == ' ' ) return (char*) buffer;
	for ( i = 0; i < siz; ++i ) {
		ch = buffer[i];
		if ( ch == fp->escape[0] || ch == fp->separator[0] ) {
			res += 1;
		}
	}
	newbuffer = (char*) malloc(siz + res + 2);
	if ( newbuffer == NULL ) return NULL;

	for ( i = 0, j = 0; i < siz; ++i ) {
		ch = buffer[i];
		if ( ch == fp->escape[0] || ch == fp->separator[0] ) {
			newbuffer[j++] = fp->escape[0];
		}
		newbuffer[j++] = ch;
	}
	newbuffer[j++] = fp->separator[0];
	newbuffer[j] = '\0';
	*size = j; 
	return newbuffer;
}

/**
 * Start a new data file to continue writing into this file.
 * It is assumed that a data write lock is already taken.
 * Take write lock for write administration.
 * Write roll-mark to the end of the current data file.
 * Create new data file generation.
 * Change Write control file.
 * Release lock.
 */
static int rolloverfile(FifoDescriptor* fwd) {
	char* filename = NULL;
	int fd = fwd->fd;
	unsigned long newcurrent;
	int lres = -1;
	int fres = -1;
	int fd2 = -1;
	int res = -1;
	char* rollmark = fwd->parameters->rollmark;
	
	if (fd >= 0) write(fd, rollmark, strlen(rollmark));

	lres = lwlock(&lockWadm);
	fres = takewritelock(fwd->fdp);
	if ( lres < 0 || fres < 0 ) {
		err("rolloverfile:");
		goto RETURN;
	}

	res = fifoReadFilePointer(fwd);
	if ( res < 0 ) {
		err("rolloverfile:");
		goto RETURN;
	}
	
	newcurrent = fwd->filePointer->current; 
	if ( newcurrent == fwd->current ) {
		newcurrent = fwd->current + 1;
	}
	filename = fifoCurrentAbsfilename(fwd->parameters->pathName, newcurrent);
	if ( filename == NULL ) {
		err("rolloverfile new filename:");
		goto RETURN;
	}
	fd2 = open(filename, O_WRONLY|O_CREAT, 0666);
	if ( fd2 < 0 ) {
		err("rolloverfile create and open new file ");
		err(filename);
		err(":");
		goto RETURN;
	}
	fwd->current = newcurrent;
	fwd->filePointer->current = newcurrent;
	res = fifoWriteFilePointer(fwd);
	if ( res < 0 ) {
		err("rolloverfile:");
		goto RETURN;
	}

	res = takewritelock(fd2);
	if ( res < 0 ) {
		err("rolloverfile:");
		goto RETURN;
	}
	close(fd);
	res = dup2(fd2, fd);
	if ( res < 0 ) {
		err("rolloverfile dup:");
	}
RETURN:
	if ( fres >= 0) releaselock(fwd->fdp);
	if ( lres >= 0) lulock(&lockWadm);
	if ( fd2 >= 0 ) {
		close(fd2);
	}
	if ( filename ) free(filename);
	return res;
}

/**
 * Write data to data file.
 * Take write lock for data file.
 * If data file would become oversized, roll file to new data file.
 * Append complete buffer into data file or write at the beginning of new file.
 * Release write lock
 */
static ssize_t writelocked(FifoDescriptor* fwd, const char* buffer, size_t size) {

	ssize_t wres = -1;
	off_t sres;
	int lres = -1;
	int fres = -1;
	int res;
	const int fd = fwd->fd;
	const off_t max = fwd->parameters->switchSize;
	
	lres = lwlock(&lockData);
	fres = takewritelock(fd);
	if ( fres < 0 || lres < 0 ) {
		err("writelocked takewritelock:");
		goto RETURN;
	}
	sres = lseek(fd, 0, SEEK_END);

	while ( sres > 0 && sres + (off_t) size > max ) {
		res = rolloverfile(fwd);
		if ( res < 0 ) {
			err("writelocked:");
			goto RETURN;
		}
		sres = lseek(fd, 0, SEEK_END);
	}
	if ( sres < 0 ) {
		err("writelocked: lseek failed:");
		goto RETURN;
	}
	wres = write(fd, buffer, size);
	if ( wres < 0 ) {
		err("writelocked write:");
	}
RETURN:
	if ( fres >= 0 ) releaselock(fd);
	if ( lres >= 0 ) lulock(&lockData);
	return wres;
}

/*************************************** READ *********************************/
/**
 * Format message to remove escape sequences.
 * NOP if escape is blank.
 */
static ssize_t fifoFormatReadBuffer(FifoParameters *fp, char* buffer, ssize_t* s) {

	ssize_t i, j;
	char ch;
	ssize_t size = *s;
	int esc = fp->escape[0] != ' ';	
	*s = -1;

	for ( i = 0, j = 0; i < size; ++i ) {
		ch = buffer[i];
		if ( esc && ch == fp->escape[0] ) {
			if ( ++i >= size ) {
				err("fifoFormatReadBuffer: incomplete escape");
				errno = EILSEQ;
				return -1;
			}
			ch = buffer[i];
		} else if ( ch == fp->separator[0] ) {
			*s = i + 1;
			break;
		}
		buffer[j++] = ch;
	}
	if ( *s <= 0 ) {
		err("fifoFormatReadBuffer: message longer than receive buffer");
		errno = E2BIG;
		return -2;
	}
	buffer[j] = '\0';
	return j;	
}

/**
 * Format output for read- or write pointer files.
 */
static int poffset( char* rbuffer, unsigned long curr, off_t o1, off_t o2 ) {
	return sprintf(rbuffer, "%lu %ld %ld %d\n", (long)curr, (long)o1, (long)o2, (int)getpid());
}

/**
 * Read form read- or write pointer file.
 */
static int readoffset(int fdadm, unsigned long* curr, off_t* o1, off_t* o2 ) {
	char rbuffer[50];
	ssize_t rres;
	int res = -1;
	off_t sres;

	*curr = 0;
	*o1 = 0;
	*o2 = 0;
	sres = lseek(fdadm, 0, SEEK_SET);
	if ( sres < 0 ) goto RETURN;
	rres = read(fdadm, rbuffer, sizeof(rbuffer));
	if ( rres < 0 ) goto RETURN;
	rbuffer[rres] = '\0';
	res = rres;
	sscanf( rbuffer, "%lu%ld%ld", curr, o1, o2);
RETURN:
	return res;
}

/**
 * Write to read- or write pointer file.
 */
static int printoffset(int fdadm, unsigned long curr, off_t o1, off_t o2, size_t lastsize) {
	ssize_t wres;
	off_t sres;
	char rbuffer[90];
	int res = -1;
	size_t size;

	poffset(rbuffer, curr, o1, o2);
	size = strlen(rbuffer);

	if ( size < lastsize ) {
		if ( ftruncate(fdadm, 0 ) < 0 ) {
			goto RETURN;
		}
	}
	sres = lseek(fdadm, 0, SEEK_SET);
	if ( sres < 0 ) goto RETURN;
	wres = write(fdadm, rbuffer, strlen(rbuffer));
	if ( wres < 0 ) goto RETURN;
	res = 0;
RETURN:
	return res;
}

/**
 * Read next message from read stream. If no message is available, return error EAGAIN.
 * If previous message has not been released return error.
 * If current message is longer than read buffer return error.
 * Take and release write lock for read admin and read lock for data.
 */
static ssize_t readlocked(FifoDescriptor* frd, char* buffer, size_t size) {

	ssize_t wres = -1;
	ssize_t osize;
	int fares = -1;
	int lares = lwlock(&lockRadm);
	int lres = -1;
	int fres = -1;
	off_t sres;
	int res;
	int fd = frd->fd;
	int fdadm = frd->fdp;
	FifoParameters *fp = frd->parameters;
	FifoFilePointer *frp = frd->filePointer;
	fares = takewritelock(fdadm);
	if ( fares < 0 || lares < 0 ) {
		err("readlocked: readadminlock:");
		goto RETURN;
	}
	lres = lrlock(&lockData);
	fres = takereadlock(fd);
	if ( lres < 0 || fres < 0 ) {
		err("readlocked: datalock:");
		goto RETURN;
	}
	res = fifoReadFilePointer(frd);
	if ( res < 0 ) goto RETURN;
	if ( frp->current != frd->current ) {
		/* re-open fd with other file */
		res = fifoReOpenRead(frd);
		if ( res < 0 && errno == ENOENT ) {
			err(NULL);
			errno = EAGAIN;
			goto RETURN;
		}
		if ( res < 0 ) {
			err("fifoRead:");
			goto RETURN;
		}
	}
	if ( frp->readPos > frp->releasePos ) {
		err("fifoRead: must first call release:");
		errno = ESPIPE;
		wres = -1; 
		goto RETURN;	/* must first call release */
	}

	sres = lseek(fd, frp->readPos, SEEK_SET);
	if ( sres < 0 ) {
		err("fifoRead lseek:");
		wres = -1;
		goto RETURN;
	}
	wres = read(fd, buffer, size);
	if ( wres < 0 ) {
		err("fifoRead read:");
		goto RETURN;
	}
	if ( wres == 0 ) {
		wres = -1;
		errno = EAGAIN;
		goto RETURN;
	}

	if (memcmp(buffer, fp->rollmark, strlen(fp->rollmark)) == 0) {
		frd->filePointer->roll = 1;
	}	

	wres = fifoFormatReadBuffer(fp, buffer, &osize);
	if ( wres < 0 ) {
		goto RETURN;
	}

	frp->readPos += osize;
RETURN:
	if ( fres >= 0 ) releaselock(fd);
	if ( lres >= 0 ) lulock(&lockData);

	if ( wres >= 0 ) {
		fifoWriteFilePointer(frd);
	}
	if ( fares >= 0 ) releaselock(fdadm);
	if ( lares >= 0 ) lulock(&lockRadm);
	return wres;
}

/**
 * Release a message from given read stream.
 * Take write locks on read administration.
 * If values of read position and release position in the read pointer
 * are not identical to what was found in the read pointer file, return
 * with error code ESPIPE.
 * Write new values into read pointer file.
 */
static ssize_t release(FifoDescriptor* frd) {

	ssize_t wres = -1;
	int res;
	int lres = lwlock(&lockRadm);
	int fdadm = frd->fdp;
	int fres = takewritelock(fdadm);
	FifoFilePointer* frp = frd->filePointer;
	off_t releasepos = frd->filePointer->releasePos;
	off_t readpos = frd->filePointer->readPos;
	int roll = frd->filePointer->roll;
	if ( lres < 0 || fres < 0 ) {
		err("release: ");
		goto RETURN;
	}
	res = fifoReadFilePointer(frd);
	if ( res < 0 ) goto RETURN;
	if ( releasepos != frp->releasePos || readpos != frp->readPos ) {
		err(NULL);
		err("release concurrent use of read pointer");
		errno = ESPIPE;
		goto RETURN;
	}
	frp->current = frd->current;
	frp->releasePos = frp->readPos;
	if ( roll || frp->readPos > frd->parameters->switchSize ) {
		frp->current += 1;
		frp->releasePos = 0;
		frp->readPos = 0;
	}
	wres = fifoWriteFilePointer(frd);
RETURN:
	if (fres >= 0) releaselock(fdadm);
	if (lres >= 0) lulock(&lockRadm);
	return wres;
}

/**
 * Read with waiting.
 * If no unread message available, sleep for a while (wtim msec) and try again until
 * maximal wait time (maxtime msec) is exceeded.
 * The time expired while waiting for locks is not taken into account.
 * If maxtime < 0, no attempt is made to read.
 * Rewturn the number of bytes read or -1 in case of error.
 */
ssize_t fifoReadW(FifoDescriptor *frd, void* buffer, size_t size, long wtim, long maxtime) {
	ssize_t rres = -1;
	struct timespec interval;
	long cumtime = 0;

	interval.tv_sec = wtim / 1000;
	interval.tv_nsec = (wtim * 1000000) % 1000000000;
	
	while ( cumtime <= maxtime ) {
		rres = readlocked(frd, buffer, size);
		if ( rres < 0 && errno == EAGAIN ) {
			nanosleep(&interval, NULL);
			errno = ETIME;
			cumtime += wtim;
		} else if ( frd->filePointer->roll == 1 ) {
			release(frd);
		} else {		
			break;
		}
		rres = -1;
	}
	return rres;
}

/**
 * Re-open read descriptor to switch reading to current file.
 */
static int fifoReOpenRead(FifoDescriptor* frd) {

	char* name;
	int res = -1;
	int fd2;
	int fres = -1;

	name = fifoCurrentAbsfilename(frd->parameters->pathName, frd->filePointer->current);
	if ( name == NULL ) {
		err("fifoOpenR fifoCurrentAbsfilename:");
		goto RETURN;
	}
	
	fd2 = open(name, O_RDONLY);
	free(name);
	if ( fd2 < 0 ) {
		err("fifoReOpenRead open ");
		err(name);
		err(":");
		goto RETURN;
	}
	/* transport held read lock to new file descriptor */
	fres = takereadlock(fd2);
	if ( fres < 0 ) {
		err("fifoReOpenRead: transport lock");
		goto RETURN;
	}
	close(frd->fd);
	res = dup2(fd2, frd->fd);
	close(fd2);
	if ( res != frd->fd ) {
		err("fifoReOpenRead dup:");
		goto RETURN;
	}
	frd->current = frd->filePointer->current;
RETURN:
	return res;
}

/**
 * Read write - or read pointer from file. No locks.
 */
static int fifoOpenFilePointer(FifoDescriptor* frd, const char* readpf) {
	int namelen;
	char* name;
	const char RPPREFIX[] = ".rp_";
	const char WPPREFIX[] = ".wp";
	FifoFilePointer* frp;

	if ( frd == NULL ) {
		err("fifoOpenFilePointer frd == NULL");
		goto RETURN;
	}

	frp = frd->filePointer;
	if ( frp == NULL ) {
		err("fifoOpenFilePointer frd->filePointer == NULL");
		goto RETURN;
	}

	frp->current = 0;
	frp->readPos = 0;
	frp->releasePos = 0;
	frp->pid = 0;

	frd->fdp = -2;
	namelen = strlen(frd->parameters->pathName) + 2;
	if ( readpf ) {
		namelen += strlen(RPPREFIX) + strlen(readpf);
	} else {
		namelen += strlen(WPPREFIX);
	}
	name = (char*) malloc(namelen);
	if ( name == NULL ) {
		err("fifoOpenFilePointer malloc:");
		goto RETURN;
	}
	strcpy(name, frd->parameters->pathName);
	strcat(name, "/");
	if ( readpf ) {
		strcat(name, RPPREFIX);
		strcat(name, readpf);
	} else {
		strcat(name, WPPREFIX);
	}
	frp->readPointerFile = name;

	frd->fdp = open(name, O_RDWR | O_CREAT, 0666);
	if ( frd->fdp < 0 ) {
		err("fifoOpenFilePointer open ");
		err(name);
		err(":");
		goto RETURN;
	}

RETURN:
	if ( frd && frd->fdp < 0 ) {
		if ( name ) {
			free(name);
			frp->readPointerFile = NULL;
		}
	}
	return frd->fdp;
}

/**
 * Write read pointer into file. No locks.
 */
static int fifoReadFilePointer(FifoDescriptor* frd) {
	int res;
	FifoFilePointer* fr = frd->filePointer;
	res = readoffset(frd->fdp, &fr->current, &fr->readPos, &fr->releasePos);
	fr->fileSize = res;
	fr->roll = 0;
	return res;
}

/**
 * Write write pointer into file. No locks.
 */
static int fifoWriteFilePointer(FifoDescriptor* frd) {
	int res;
	FifoFilePointer* fr = frd->filePointer;
	res = printoffset(frd->fdp, fr->current, fr->readPos, fr->releasePos, fr->fileSize);
	return res;
}

/**
 * Call the pthread interface for read-write locks.
 * Provide the function pointer and a lock pointer.
 * Set the errno variable from the return value.
 */
static int doplock(int (*pcall)(pthread_rwlock_t* lock), pthread_rwlock_t* lock) {
	int res = (*pcall)(lock);
	if ( res != 0 ) {
		errno = res; res = -1;
	}
	return res;
}

static int lwlock(pthread_rwlock_t* lock) {
	return doplock(&pthread_rwlock_wrlock, lock);
}

static int lrlock(pthread_rwlock_t* lock) {
	return doplock(&pthread_rwlock_rdlock, lock);
}

static int lulock(pthread_rwlock_t* lock) {
	return doplock(&pthread_rwlock_unlock, lock);
}

/**
 * Call the advisory file lock interface on the whole file
 * deternined by the open file descriptor.
 * Supported lock types are UNLOCK, RDLOCK, and WRLOCK
 */
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

static int takereadlock(int fd ) {
	return dolock(fd, F_RDLCK);
}

static int releaselock(int fd ) {
	return dolock(fd, F_UNLCK);
}

/**
 * Initialize error text stack (if text argument is NULL
 * or append text to error text stack
 * If stack area would overflow, ignore request
 */
static void err( const char* text ) {

	if ( text == NULL ) {
		fifoERROR[0] = '\0';
		return;
	}

	if ( strlen(fifoERROR) + strlen(text) + 2 > sizeof(fifoERROR) ) {
		return;
	}

	strcat(fifoERROR, text);
	if ( text[strlen(text)-1] == ':' ) strcat(fifoERROR, "\n");
}

/* END OF SOURCE FILE */
