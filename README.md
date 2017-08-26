filequeue
=========

C-library to store and retrieve data items sequentially

The project provides source files `fifop.c` and`fifo.c`, which define
the library functions and
prototype/test programs `fifomain.c`, `fifor.c`, and `fifow.c`

Principle:
Implement a multiple-reader - multiple-writer fifo queue in file system storage.

One or more user writer-programs want to send messages sequentially to a buffering queue.
The system maintains a single write-pointer for each queue.
The messages may be any human-readable or binary data.
The messages are not messed up in the buffering device.
Any number of reader-programs can read messages from the queue.
Reader programs may use individual or shared read-pointers.
Each message is retrievable once for each read-pointer.
Once a message is processed, it has to be released for that read-pointer.
No messages are actually removed from storage.

The program version `fifo.c` uses file locks, whereas `fifop.c` uses `phthread` locks.
The public interfaces in `fifo.h` and `fifop.h` don't differ.

`fifomain.c`is a general testing program.
`fifor.c` and `fifow.c` are early proof-of-concept versions. See docu in `fifow.c`.

Usage:
```
#include	"fifo.h"
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
int fifoCreate( const char* dirname, off_t switchSize, char esc, char sep );

/**
 * Open file for writing.
 * Take write locks during change of the write pointer file.
 * The write pointer structure must be used in the same thread, which opened it.
 * Return write pointer.
 */
FifoDescriptor* fifoOpenW( const char* filename );

/**
 * Open read stream for the file queue. Multiple read streams, identified by
 * a unique name, can operate on the same file queue.
 * Take a read lock on the read pointer file during operation.
 * The returned read pointer has to be used for all read and release operations
 * for this read stream.
 * Return read pointer.
 */
FifoDescriptor* fifoOpenR( const char* filename, const char* readpf);

/**
 * Write a message to the file queue.
 * Format the message and write to current data file.
 */
ssize_t fifoWrite( FifoDescriptor* fwd, void* buffer, size_t size );

/**
 * Read a message from open read stream of file queue.
 * The message must fit into the provided buffer.
 * Undo the message formatting done during write.
 */
ssize_t fifoRead( FifoDescriptor* frd, void* buffer, size_t size );

/**
 * Release the previously read message from the open read stream.
 * If no unreleased message exists, silently ignore this call.
 * Note: only data files, which do not contain unreleased messages by any
 * read pointer may be removed from file system.
 */
ssize_t fifoRelease(FifoDescriptor* frd);

/**
 * Close read pointer.
 */
void fifoCloseR( FifoDescriptor* fp );

/**
 * Close write pointer.
 */
void fifoCloseW( FifoDescriptor* fp );

/*************** END OF PUBLIC INTERFACE *************************************/
```
