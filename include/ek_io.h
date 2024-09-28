#ifndef EK_IO_H
#define EK_IO_H

#include <stdint.h>
#include "ek_fifo.h"

#ifdef __linux__
#include <sys/epoll.h>
#else
#include <sys/event.h>
#endif

// Define a reasonable event list size for epoll/kqueue
#define EK_EVENT_LIST_SIZE 256

// Forward declaration of ek_completion
struct ek_completion;

// Structure for the event loop (ek_io)
typedef struct ek_io {
	int	io_fd;
	size_t	io_inflight;
	ek_fifo timeouts;
	ek_fifo completed;
	ek_fifo io_pending;
} ek_io;

// Operation types for ek_io
enum ek_op_type {
	EK_OP_ACCEPT,
	EK_OP_CONNECT,
	EK_OP_READ,
	EK_OP_WRITE,
	EK_OP_RECV,
	EK_OP_SEND,
	EK_OP_TIMEOUT,
};

// Structure for operations
typedef struct ek_operation {
	enum ek_op_type op_type;
	union {
		struct { int socket; } accept;
		struct { int fd; char *buf; size_t len; off_t offset; } read;
		struct { int fd; const char *buf; size_t len; off_t offset; } write;
		struct { int socket; } recv;
		struct { int socket; } send;
		struct { uint64_t expires; } timeout;
	} data;
} ek_operation;

// Structure for completion of operations
typedef struct ek_completion {
	ek_fifo_node	node;
	void *		context;
	void (*callback)(struct ek_io *io, struct ek_completion *completion);
	ek_operation *	operation;
} ek_completion;

// Common Function Prototypes
void ek_io_init(ek_io *io);
void ek_io_deinit(ek_io *io);
void ek_io_tick(ek_io *io);
void ek_io_run_for_ns(ek_io *io, uint64_t nanoseconds);
void ek_io_flush(ek_io *io, bool wait_for_completions);
uint64_t ek_io_flush_timeouts(ek_io *io);
uint64_t ek_io_current_time_ns(void);


#ifdef __linux__
#include <sys/epoll.h>
size_t ek_io_flush_io(ek_io *io, struct epoll_event *events, ek_completion **io_pending_top);
#else
#include <sys/event.h>
size_t ek_io_flush_io(ek_io *io, struct kevent *events, ek_completion **io_pending_top);
#endif

#endif // EK_IO_H
