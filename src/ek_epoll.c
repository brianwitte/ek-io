// SPDX-License-Identifier: Apache-2.0 OR MIT
/*
 * This file is licensed under either:
 *
 * - Apache License, Version 2.0 (LICENSE-APACHE or http://www.apache.org/licenses/LICENSE-2.0)
 * - MIT license (LICENSE-MIT or http://opensource.org/licenses/MIT)
 *
 * at your option.
 *
 * Contribution:
 *
 * Unless you explicitly state otherwise, any contribution intentionally
 * submitted for inclusion in the work by you, as defined in the Apache-2.0
 * license, shall be dual licensed as above, without any additional terms or
 * conditions.
 *
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <stdint.h>

#include "ek_fifo.h"
#include "ek_io.h"

// Static callback function to handle timeouts
static void timeout_callback(ek_io *io, ek_completion *comp)
{
	(void)io; // Unused in this case
	*(bool *)comp->context = true;
}

// Initialize the IO event loop (epoll-based)
void ek_io_init(ek_io *io)
{
	io->io_fd = epoll_create1(0);
	if (io->io_fd == -1) {
		perror("epoll_create1 failed");
		exit(EXIT_FAILURE);
	}
	io->io_inflight = 0;
	ek_fifo_init(&io->timeouts);
	ek_fifo_init(&io->completed);
	ek_fifo_init(&io->io_pending);
}

// Deinitialize the IO event loop (epoll-based)
void ek_io_deinit(ek_io *io)
{
	assert(io->io_fd > -1);
	close(io->io_fd);
	io->io_fd = -1;
}

// Handle non-blocking processing of events (epoll)
void ek_io_tick(ek_io *io)
{
	ek_io_flush(io, false);
}


uint64_t ek_io_current_time_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

// Run the event loop for a specific duration (nanoseconds) (epoll)
void ek_io_run_for_ns(ek_io *io, uint64_t nanoseconds)
{
	bool timed_out = false;
	ek_completion completion;

	// Set up the completion structure and callback for the timeout
	completion.callback = timeout_callback;
	completion.context = &timed_out;

	// Set up the timeout operation (assuming the timeout is in nanoseconds)
	ek_operation timeout_op;
	timeout_op.op_type = EK_OP_TIMEOUT;
	timeout_op.data.timeout.expires = ek_io_current_time_ns() + nanoseconds; // Implement `ek_current_time_ns`

	// Attach the timeout operation to the completion
	completion.operation = &timeout_op;

	// Push the timeout operation into the pending queue
	ek_fifo_push(&io->timeouts, &completion); // Push to the timeouts queue

	// Flush I/O to start processing events
	ek_io_flush(io, true);

	// Loop until timeout
	while (!timed_out)
		ek_io_flush(io, true);
}


// Flush timeouts and pending I/O operations (epoll-based)
void ek_io_flush(ek_io *io, bool wait_for_completions)
{
	struct epoll_event events[EK_EVENT_LIST_SIZE];
	int new_events; // Declare at the top (C89/C90 standards)
	size_t change_events = 0;
	ek_completion *io_pending =
		(ek_completion *)ek_fifo_peek(&io->io_pending);
	ek_completion *completed = NULL; // Declare all variables at the top

	uint64_t next_timeout = ek_io_flush_timeouts(io);

	change_events = ek_io_flush_io(io, events, &io_pending);

	// Handle epoll events if there are pending changes or no completions
	if (change_events > 0 || ek_fifo_empty(&io->completed)) {
		int timeout_ms = wait_for_completions ? (int)(next_timeout /
							      1000000) : 0;
		if (change_events == 0 && ek_fifo_empty(&io->completed) &&
		    io->io_inflight == 0)
			return;

		new_events = epoll_wait(io->io_fd, events, EK_EVENT_LIST_SIZE,
					timeout_ms);
		if (new_events < 0) {
			perror("epoll_wait failed");
			exit(EXIT_FAILURE);
		}

		io->io_inflight += change_events;
		io->io_inflight -= (size_t)new_events;

		for (int i = 0; i < new_events; i++) {
			ek_completion *completion =
				(ek_completion *)events[i].data.ptr;
			ek_fifo_push(&io->completed, completion);
		}

		// Process completed operations
		while ((completed =
				(ek_completion *)ek_fifo_pop(&io->completed)) !=
		       NULL)
			completed->callback(io, completed);
	}
}

// Flush I/O operations and populate epoll events

size_t ek_io_flush_io(ek_io *io, struct epoll_event *events,
		      ek_completion **io_pending_top)
{
	size_t flushed = 0;
	int op_type = 0;                // Declare at the top
	int ident = 0;                  // Declare at the top
	bool is_registered = false;     // Track registration status

	while (flushed < EK_EVENT_LIST_SIZE) {
		ek_completion *completion = *io_pending_top;
		if (!completion) break;

		*io_pending_top = (ek_completion *)completion->node.next; // Use node.next

		// Handle each operation type
		switch (completion->operation->op_type) {
		case EK_OP_ACCEPT:
			ident = completion->operation->data.accept.socket;
			op_type = EPOLLIN;
			break;
		case EK_OP_CONNECT:
			ident = completion->operation->data.accept.socket;
			op_type = EPOLLOUT;
			break;
		case EK_OP_READ:
			ident = completion->operation->data.read.fd;
			op_type = EPOLLIN;
			break;
		case EK_OP_WRITE:
			ident = completion->operation->data.write.fd;
			op_type = EPOLLOUT;
			break;
		case EK_OP_RECV:
			ident = completion->operation->data.recv.socket;
			op_type = EPOLLIN;
			break;
		case EK_OP_SEND:
			ident = completion->operation->data.send.socket;
			op_type = EPOLLOUT;
			break;
		case EK_OP_TIMEOUT:
			continue;
		}

		events[flushed].events = (unsigned int)op_type | EPOLLONESHOT;
		events[flushed].data.ptr = completion;

		struct epoll_event ev = events[flushed];

		// Check if the file descriptor is already registered
		if (!is_registered) {
			// Try to add the file descriptor
			if (epoll_ctl(io->io_fd, EPOLL_CTL_ADD, ident,
				      &ev) < 0) {
				if (errno == EEXIST) {
					// If it already exists, modify it instead
					if (epoll_ctl(io->io_fd, EPOLL_CTL_MOD,
						      ident, &ev) < 0) {
						perror(
							"epoll_ctl (modify) failed");
						exit(EXIT_FAILURE);
					}
				} else {
					perror("epoll_ctl (add) failed");
					exit(EXIT_FAILURE);
				}
			}
			is_registered = true; // Mark as registered after successful operation
		} else {
			// If already registered, modify the existing entry
			if (epoll_ctl(io->io_fd, EPOLL_CTL_MOD, ident,
				      &ev) < 0) {
				perror("epoll_ctl (modify) failed");
				exit(EXIT_FAILURE);
			}
		}

		flushed++;
	}

	return flushed;
}

// Flush timeouts and return the next timeout in nanoseconds
uint64_t ek_io_flush_timeouts(ek_io *io)
{
	uint64_t min_timeout = UINT64_MAX;
	ek_completion *completion =
		(ek_completion *)ek_fifo_peek(&io->timeouts);

	while (completion) {
		uint64_t now = 0; // Replace with appropriate time call
		if (now >= completion->operation->data.timeout.expires) {
			ek_fifo_remove(&io->timeouts, completion);
			ek_fifo_push(&io->completed, completion);
		} else {
			uint64_t timeout_ns =
				completion->operation->data.timeout.expires - now;
			if (timeout_ns < min_timeout)
				min_timeout = timeout_ns;
		}
		completion = (ek_completion *)completion->node.next; // Use node.next
	}
	return min_timeout;
}
