// SPDX-License-Identifier: Apache-2.0 OR MIT
/*
 * This file is licensed under either:
 *
 * - Apache License, Version 2.0 (LICENSE-APACHE or http://www.apache.org/licenses/LICENSE-2.0)
 * - MIT license (LICENSE-MIT or http://opensource.org/licenses/MIT)
 *
 * at your option.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <sys/event.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "ek_fifo.h"
#include "ek_io.h"

// Static callback function to handle timeouts
static void timeout_callback(ek_io *io, ek_completion *comp)
{
	(void)io; // Unused in this case
	*(bool *)comp->context = true;
}

// Initialize the IO event loop
void ek_io_init(ek_io *io)
{
	io->io_fd = kqueue();
	assert(io->io_fd > -1);
	io->io_inflight = 0;
	ek_fifo_init(&io->timeouts);
	ek_fifo_init(&io->completed);
	ek_fifo_init(&io->io_pending);
}

// Deinitialize the IO event loop
void ek_io_deinit(ek_io *io)
{
	assert(io->io_fd > -1);
	close(io->io_fd);
	io->io_fd = -1;
}

// Handle non-blocking processing of events
void ek_io_tick(ek_io *io)
{
	ek_io_flush(io, false);
}

// Run the event loop for a specific duration (nanoseconds)
void ek_io_run_for_ns(ek_io *io, uint64_t nanoseconds)
{
	bool timed_out = false;
	ek_completion completion;

	// Set up the completion structure and callback for the timeout
	completion.callback = timeout_callback;
	completion.context = &timed_out;

	// Push the timeout operation into the pending queue
	ek_io_flush(io, true);

	// Loop until timeout
	while (!timed_out)
		ek_io_flush(io, true);
}

// Flush timeouts and pending I/O operations
void ek_io_flush(ek_io *io, bool wait_for_completions)
{
	struct kevent events[EK_EVENT_LIST_SIZE];
	int new_events; // Declare at the top (C89/C90 standards)
	size_t change_events = 0;
	ek_completion *io_pending =
		(ek_completion *)ek_fifo_peek(&io->io_pending);
	ek_completion *completed = NULL; // Declare all variables at the top

	uint64_t next_timeout = ek_io_flush_timeouts(io);

	change_events = ek_io_flush_io(io, events, &io_pending);

	// Handle kqueue events if there are pending changes or no completions
	if (change_events > 0 || ek_fifo_empty(&io->completed)) {
		struct timespec ts = { 0 };
		if (change_events == 0 && ek_fifo_empty(&io->completed)) {
			if (wait_for_completions) {
				ts.tv_sec = next_timeout / 1000000000;
				ts.tv_nsec = next_timeout % 1000000000;
			} else if (io->io_inflight == 0) {
				return;
			}
		}

		new_events = kevent(io->io_fd, events, (int)change_events,
				    events, EK_EVENT_LIST_SIZE, &ts);
		if (new_events < 0) {
			perror("kevent failed");
			exit(EXIT_FAILURE);
		}

		io->io_inflight += change_events;
		io->io_inflight -= (size_t)new_events;

		for (int i = 0; i < new_events; i++) {
			ek_completion *completion =
				(ek_completion *)events[i].udata;
			ek_fifo_push(&io->completed, completion);
		}

		// Process completed operations
		while ((completed =
				(ek_completion *)ek_fifo_pop(&io->completed)) !=
		       NULL)
			completed->callback(io, completed);
	}
}

// Flush I/O operations and populate kevents
size_t ek_io_flush_io(ek_io *io, struct kevent *events,
		      ek_completion **io_pending_top)
{
	size_t flushed = 0;
	int filter = EVFILT_READ;       // Declare at the top
	int ident = 0;                  // Declare at the top

	while (flushed < EK_EVENT_LIST_SIZE) {
		ek_completion *completion = *io_pending_top;
		if (!completion) break;

		*io_pending_top = (ek_completion *)completion->node.next; // Use node.next

		// Handle each operation type
		switch (completion->operation->op_type) {
		case EK_OP_ACCEPT:
			ident = completion->operation->data.accept.socket;
			filter = EVFILT_READ;
			break;
		case EK_OP_CONNECT:
			ident = completion->operation->data.accept.socket;
			filter = EVFILT_WRITE;
			break;
		case EK_OP_READ:
			ident = completion->operation->data.read.fd;
			filter = EVFILT_READ;
			break;
		case EK_OP_WRITE:
			ident = completion->operation->data.write.fd;
			filter = EVFILT_WRITE;
			break;
		case EK_OP_RECV:
			ident = completion->operation->data.recv.socket;
			filter = EVFILT_READ;
			break;
		case EK_OP_SEND:
			ident = completion->operation->data.send.socket;
			filter = EVFILT_WRITE;
			break;
		case EK_OP_TIMEOUT:
			continue; // No kevent for timeout, handled separately
			// Removed 'default' to avoid -Wcovered-switch-default error
		}

		EV_SET(&events[flushed], ident, filter,
		       EV_ADD | EV_ENABLE | EV_ONESHOT, 0, 0, completion);
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
				completion->operation->data.timeout.expires -
				now;
			if (timeout_ns < min_timeout)
				min_timeout = timeout_ns;
		}
		completion = (ek_completion *)completion->node.next; // Use node.next
	}
	return min_timeout;
}
