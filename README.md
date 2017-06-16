filequeue
=========

C-library to store and retrieve data items sequentially

The project provides source files fifop.c and fifo.c, which define
the library functions and
prototype/test programs fifomain.c, fifor.c, and fifow.c

Principle:
Implement a multiple-reader - multiple-writer fifo queue in file system storage.

One or more user writer-programs wants to send messages sequentially to a buffering queue.
The system maintains a single write-pointer for each queue.
The messages may be any human-readable or binary data.
The messages are not messed up in the buffering device.
Any number of reader-programs can read messages from the queue.
Reader programs may use individual or shared read-pointers.
Each message is retrievable once for each read-pointer.
Once a message is processed, it has to be released for that read-pointer.
No messages are actually removed from storage.

API:

see source code.



