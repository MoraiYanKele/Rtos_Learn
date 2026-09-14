#pragma once

#include "schedule.h"

#define CONFIG_HEAP                     8 * 1024
#define ALIGNMENT_MASK                  (uintptr_t)0x07

#define MIN_SIZE                        ((size_t)(heapStructSize << 1))



Class(heap_node) {
    heap_node *next;
    size_t blockSize;
};

Class(xheap) {
    heap_node head;
    heap_node *tail;
    size_t allSize;
};

xheap theHeap = {
    .tail = NULL,
    .allSize = CONFIG_HEAP,
};


void Heap_Init(void);
void *Heap_Malloc(size_t _want_size);
void Heap_Free(void *_free_ptr);