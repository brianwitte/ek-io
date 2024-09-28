#ifndef EK_FIFO_H
#define EK_FIFO_H

#include <stdbool.h>

// Generic FIFO structure (handles any type by using void pointers)
typedef struct ek_fifo_node {
    void *next;
} ek_fifo_node;

typedef struct ek_fifo {
    ek_fifo_node* in;
    ek_fifo_node* out;
} ek_fifo;

// Function prototypes
void ek_fifo_init(ek_fifo* fifo);
void ek_fifo_push(ek_fifo* fifo, void* elem);
void* ek_fifo_pop(ek_fifo* fifo);
void* ek_fifo_peek(ek_fifo* fifo);
bool ek_fifo_empty(ek_fifo* fifo);
void ek_fifo_remove(ek_fifo* fifo, void* to_remove);

#endif // EK_FIFO_H
