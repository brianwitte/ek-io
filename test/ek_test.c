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
#include <unistd.h>
#include <assert.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "ek_fifo.h"
#include "ek_io.h"

// Test function for ek_fifo with generic elements
static int test_fifo(void)
{
	// Define three generic nodes for the test
	ek_fifo_node one = { NULL };
	ek_fifo_node two = { NULL };
	ek_fifo_node three = { NULL };

	// Initialize the FIFO
	ek_fifo fifo;

	ek_fifo_init(&fifo);

	// Check that the FIFO is initially empty
	assert(ek_fifo_empty(&fifo));

	// Push the first node
	ek_fifo_push(&fifo, &one);
	assert(!ek_fifo_empty(&fifo));
	assert(ek_fifo_peek(&fifo) == &one);

	// Push the second and third nodes
	ek_fifo_push(&fifo, &two);
	ek_fifo_push(&fifo, &three);
	assert(!ek_fifo_empty(&fifo));
	assert(ek_fifo_peek(&fifo) == &one);

	// Remove the first node and check the order
	ek_fifo_remove(&fifo, &one);
	assert(!ek_fifo_empty(&fifo));
	assert(ek_fifo_pop(&fifo) == &two);
	assert(ek_fifo_pop(&fifo) == &three);
	assert(ek_fifo_pop(&fifo) == NULL);
	assert(ek_fifo_empty(&fifo));

	printf("FIFO test passed!\n");
	return 0;
}
// A simple callback function for I/O operations (used in test_io)
static void test_io_callback(ek_io *io, ek_completion *completion)
{
	(void)io; // Silence unused parameter warnings

	printf("I/O completion callback invoked for operation type %d\n",
	       completion->operation->op_type);
}

// Test function for ek_io
static int test_io(void)
{
	ek_io io;
	ek_completion completion;
	ek_operation operation;
	struct sockaddr_in server_addr;

	// Create a socket for testing
	int server_fd = socket(AF_INET, SOCK_STREAM, 0);

	if (server_fd < 0) {
		perror("socket failed");
		exit(EXIT_FAILURE);
	}

	// Set up the server address
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(8080);

	// Bind the socket
	if (bind(server_fd, (struct sockaddr *)&server_addr,
		 sizeof(server_addr)) < 0) {
		perror("bind failed");
		close(server_fd);
		exit(EXIT_FAILURE);
	}

	// Listen on the socket
	if (listen(server_fd, 10) < 0) {
		perror("listen failed");
		close(server_fd);
		exit(EXIT_FAILURE);
	}

	ek_io_init(&io);

	// Simulate an accept operation
	operation.op_type = EK_OP_ACCEPT;
	operation.data.accept.socket = server_fd;

	completion.node.next = NULL;
	completion.operation = &operation;
	completion.context = NULL;
	completion.callback = test_io_callback;

	// Push the completion to io_pending and process it
	ek_fifo_push(&io.io_pending, &completion);
	ek_io_flush(&io, false);

	// Simulate a read operation
	operation.op_type = EK_OP_READ;
	operation.data.read.fd = server_fd;     // Use the real socket file descriptor
	operation.data.read.buf = NULL;         // Normally a buffer would be provided
	operation.data.read.len = 1024;
	operation.data.read.offset = 0;

	completion.operation = &operation;
	completion.callback = test_io_callback;

	// Push the completion to io_pending and process it
	ek_fifo_push(&io.io_pending, &completion);
	ek_io_flush(&io, false);

	printf("I/O test passed!\n");

	ek_io_deinit(&io);
	close(server_fd); // Clean up the socket
	return 0;
}

int main(void)
{
	test_fifo();
	test_io();
	return 0;
}
