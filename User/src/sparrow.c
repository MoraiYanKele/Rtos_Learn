#include <stdint.h>
#include <stdlib.h>

#include "sparrow.h"

#define CONFIG_HEAP         8 * 1024
#define ALIGNMENT_MASK      (uintptr_t)0x07

#define MIN_SIZE            ((size_t)(heapStructSize << 1))

#define Class(class)            \
    typedef struct class class; \
    struct class



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

static const size_t heapStructSize = (sizeof(heap_node) + (size_t)(ALIGNMENT_MASK)) & ~((size_t)ALIGNMENT_MASK);

static uint8_t allHeap[CONFIG_HEAP];

static void InsertFreeBlock(heap_node *insertBlockPtr);

void Heap_Init(void) {
    heap_node *first_node;
    
    uintptr_t raw_start = (uintptr_t)allHeap;
    uintptr_t raw_end = raw_start + CONFIG_HEAP;

    uintptr_t start_heap = (raw_start + ALIGNMENT_MASK) & ~(uintptr_t)ALIGNMENT_MASK;
    uintptr_t end_heap = (raw_end - heapStructSize) & ~(uintptr_t)ALIGNMENT_MASK;

    theHeap.allSize = (size_t)(end_heap - start_heap);
    
    theHeap.head.next = (heap_node *)start_heap;
    theHeap.head.blockSize = (size_t)0;

    theHeap.tail = (heap_node *)end_heap;
    theHeap.tail->blockSize = (size_t)0;
    theHeap.tail->next = NULL;

    first_node = (heap_node *)start_heap;
    first_node->next = theHeap.tail;
    first_node->blockSize = theHeap.allSize;    
}

void *Heap_Malloc(size_t wantSize) {
    heap_node *prev_node;
    heap_node *use_node;
    heap_node *new_node;
    size_t want_size = 0;;
    void *return_ptr = NULL;

    if (wantSize == 0) {
        return NULL;
    }

    want_size = (wantSize + heapStructSize + ALIGNMENT_MASK) & ~(size_t)ALIGNMENT_MASK;

    if (theHeap.tail == NULL) {
        Heap_Init();
    }

    prev_node = &theHeap.head;
    use_node = theHeap.head.next;

    while (use_node->blockSize < want_size) {
        prev_node = use_node;
        use_node = use_node->next;
        if (use_node == NULL) {
            return return_ptr;
        }
    }

    return_ptr = (void *)(((uint8_t *)use_node) + heapStructSize);

    prev_node->next = use_node->next;
    
    if ((use_node->blockSize - want_size) > MIN_SIZE) {
        new_node = (void *)((uint8_t *)use_node + want_size);
        
        new_node->blockSize = use_node->blockSize - want_size;
        use_node->blockSize = want_size;
        
        new_node->next = prev_node->next;
        prev_node->next = new_node;
    }

    theHeap.allSize -= use_node->blockSize;
    use_node->next = NULL;

    return return_ptr;
}

void Heap_Free(void *freePtr) {
    heap_node *link_ptr;
    uint8_t *free_ptr = (uint8_t *)freePtr;

    free_ptr -= heapStructSize;
    link_ptr = (heap_node *)free_ptr;
    
    theHeap.allSize += link_ptr->blockSize;
    InsertFreeBlock(link_ptr);
}

static void InsertFreeBlock(heap_node* insertBlockPtr) {
    
}
