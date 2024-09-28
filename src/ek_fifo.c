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
#include <stdbool.h>
#include <assert.h>
#include "ek_fifo.h"

// Initialize the FIFO queue
void ek_fifo_init(ek_fifo *fifo)
{
	fifo->in = NULL;
	fifo->out = NULL;
}

// Push an element into the FIFO queue
void ek_fifo_push(ek_fifo *fifo, void *elem)
{
	ek_fifo_node *node = (ek_fifo_node *)elem;

	node->next = NULL;
	if (fifo->in) {
		((ek_fifo_node *)fifo->in)->next = node;
		fifo->in = node;
	} else {
		fifo->in = node;
		fifo->out = node;
	}
}

// Pop an element from the FIFO queue
void * ek_fifo_pop(ek_fifo *fifo)
{
	ek_fifo_node *ret = (ek_fifo_node *)fifo->out;

	if (ret == NULL) return NULL;
	fifo->out = ret->next;
	if (fifo->in == ret) fifo->in = NULL;
	return ret;
}

// Peek at the next element in the FIFO queue
void * ek_fifo_peek(ek_fifo *fifo)
{
	return fifo->out;
}

// Check if the FIFO queue is empty
bool ek_fifo_empty(ek_fifo *fifo)
{
	return ek_fifo_peek(fifo) == NULL;
}

// Remove a specific element from the FIFO queue
void ek_fifo_remove(ek_fifo *fifo, void *to_remove)
{
	ek_fifo_node *it = (ek_fifo_node *)fifo->out;

	if (to_remove == fifo->out) {
		ek_fifo_pop(fifo);
		return;
	}
	while (it != NULL) {
		if (it->next == to_remove) {
			if (to_remove == fifo->in)
				fifo->in = it;
			it->next = ((ek_fifo_node *)to_remove)->next;
			((ek_fifo_node *)to_remove)->next = NULL;
			break;
		}
		it = it->next;
	}
}
