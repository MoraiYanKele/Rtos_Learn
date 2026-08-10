#include <stddef.h>
#include <stdint.h>

#include "kernel/heap.h"
#include "port/port_def.h"
#include "sparrow_conf.h"
#include "sparrow_def.h"

/* 块头。分配出去的块也带着这个头，HeapFree 靠它找回块大小。 */
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
    /* 这里只是个占位值，HeapInit 会用对齐之后的真实可用大小覆盖它。 */
    .allSize = CONFIG_HEAP,
};

/* 块头本身占的字节数，向上对齐到 8 字节。 */
static const size_t heapStructSize = (sizeof(heap_node) + (size_t)(ALIGNMENT_MASK)) & ~((size_t)ALIGNMENT_MASK);

/* 切分空闲块时，剩下的部分至少要能装下一个块头加一点有效载荷，否则不值得切。 */
#define MIN_SIZE            ((size_t)(heapStructSize << 1))

static uint8_t allHeap[CONFIG_HEAP];

static void InsertFreeBlock(heap_node *insert_block_ptr);

void HeapInit(void) {
    heap_node *first_node;

    uintptr_t raw_start = (uintptr_t)allHeap;
    uintptr_t raw_end = raw_start + CONFIG_HEAP;

    /* 起点向上对齐、终点向下对齐，中间那段才是真正能用的范围。
     * 终点还要再退掉一个块头的位置，用来安放 tail 哨兵。 */
    uintptr_t start_heap = (raw_start + ALIGNMENT_MASK) & ~(uintptr_t)ALIGNMENT_MASK;
    uintptr_t end_heap = (raw_end - heapStructSize) & ~(uintptr_t)ALIGNMENT_MASK;

    theHeap.allSize = (size_t)(end_heap - start_heap);

    theHeap.head.next = (heap_node *)start_heap;
    theHeap.head.blockSize = (size_t)0;

    theHeap.tail = (heap_node *)end_heap;
    theHeap.tail->blockSize = (size_t)0;
    theHeap.tail->next = NULL;

    /* 一开始整块内存就是一个大空闲块。 */
    first_node = (heap_node *)start_heap;
    first_node->next = theHeap.tail;
    first_node->blockSize = theHeap.allSize;
}

void *HeapMalloc(size_t req_size) {
    heap_node *prev_node;
    heap_node *use_node;
    heap_node *new_node;
    size_t want_size = 0;
    void *return_ptr = NULL;

    if (req_size == 0) {
        return NULL;
    }

    /* 实际要占的大小 = 请求大小 + 块头，再向上对齐。 */
    want_size = (req_size + heapStructSize + ALIGNMENT_MASK) & ~(size_t)ALIGNMENT_MASK;

    if (theHeap.tail == NULL) {
        HeapInit();
    }

    /* first-fit：从头找第一块装得下的。tail 的 blockSize 是 0，永远装不下，
     * 所以走到 tail 之后下一步就是 NULL，正好作为"找不到"的出口。 */
    prev_node = &theHeap.head;
    use_node = theHeap.head.next;

    while (use_node->blockSize < want_size) {
        prev_node = use_node;
        use_node = use_node->next;
        if (use_node == NULL) {
            return return_ptr;
        }
    }

    /* 返回给调用者的是块头之后的地址。 */
    return_ptr = (void *)(((uint8_t *)use_node) + heapStructSize);

    prev_node->next = use_node->next;

    /* 剩余部分够大就切出一个新空闲块挂回链表，否则整块给出去（内部碎片）。 */
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

void HeapFree(void *ptr) {
    heap_node *link_ptr;
    uint8_t *free_ptr = (uint8_t *)ptr;

    /* 往前退一个块头，就回到了当初分配出去的那个 heap_node。 */
    free_ptr -= heapStructSize;
    link_ptr = (heap_node *)free_ptr;

    theHeap.allSize += link_ptr->blockSize;
    InsertFreeBlock(link_ptr);
}

static void InsertFreeBlock(heap_node *insert_block_ptr) {
    heap_node *first_fit_node = NULL;
    uint8_t *get_addr = NULL;

    if (insert_block_ptr == NULL) {
        return;
    }

    /* 按地址升序找插入点。链表保持地址有序，相邻的空闲块在链表上也相邻，
     * 合并时才只需要看前后两个邻居。 */
    for (first_fit_node = &theHeap.head; first_fit_node->next != theHeap.tail && first_fit_node->next < insert_block_ptr; first_fit_node = first_fit_node->next) {
        // finding the fit node
    }

    insert_block_ptr->next = first_fit_node->next;
    first_fit_node->next = insert_block_ptr;

    /* 向后合并：本块尾部正好接上下一块的开头就并过来。
     * 下一块是 tail 时不能并 —— tail 只是哨兵，不代表真实内存。 */
    get_addr = (uint8_t *)insert_block_ptr;
    if ((get_addr + insert_block_ptr->blockSize) == (uint8_t *)(insert_block_ptr->next)) {
        if (insert_block_ptr->next != theHeap.tail) {
            insert_block_ptr->blockSize += insert_block_ptr->next->blockSize;
            insert_block_ptr->next = insert_block_ptr->next->next;
        }
    }

    /* 向前合并：前一块尾部正好接上本块的开头就并进去。 */
    get_addr = (uint8_t *)first_fit_node;
    if (get_addr + first_fit_node->blockSize == (uint8_t *)insert_block_ptr) {
        first_fit_node->blockSize += insert_block_ptr->blockSize;
        first_fit_node->next = insert_block_ptr->next;
    }
}
