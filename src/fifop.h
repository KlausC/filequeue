
#define _POSIX_SOURCE
#include <sys/types.h>
#include	<unistd.h>

typedef
struct {
	char*	pathName;	/* absolute name of directory */
	off_t	switchSize;	/* if file size greater: new generation */
	char	escape[2];		/* mask special characters if record bounds */
	char	separator[2];	/* record separator */
	char	rollmark[4];	/* roll mark = escape '@' separator */
}	FifoParameters;

typedef
struct	{
	char*	readPointerFile;/* name of read pointer file in dir */
	unsigned long	current;/* current file number for reading */
	off_t	readPos;	/* position of next byte to read */
	off_t	releasePos;	/* position of last byte released */
	pid_t	pid;		/* pid of process that read last */
	size_t	fileSize;	/* file size read */
	int	roll;		/* indicate that next release shall roll to next file */
}	FifoFilePointer;	


typedef
struct	{
	FifoParameters* parameters;
	FifoFilePointer* filePointer;
	unsigned long	current;/* number of current write file */
	int	fd;
	int	fdp;		/* fd of read pointer file */
}	FifoDescriptor;

int fifoCreate(const char* dirname, off_t swithSize, char esc, char sep);
FifoDescriptor* fifoOpenW(const char* filename);
FifoDescriptor* fifoOpenR(const char* filename, const char* readpointer);
ssize_t fifoWrite(FifoDescriptor* fp, void* buffer, size_t size);
ssize_t fifoRead(FifoDescriptor* fp, void* buffer, size_t size);
ssize_t fifoReadW(FifoDescriptor* frd, void* buffer, size_t size, long wtim, long maxtim);
ssize_t fifoRelease(FifoDescriptor* fp);
void fifoCloseR(FifoDescriptor* fp);
void fifoCloseW(FifoDescriptor* fp);


