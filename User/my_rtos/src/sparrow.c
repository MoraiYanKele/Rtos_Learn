#include <stdint.h>
#include <stdlib.h>

#include "sparrow.h"
#include "ws2812.h"
#include "main.h"

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

void *Heap_Malloc(size_t _want_size) {
    heap_node *prev_node;
    heap_node *use_node;
    heap_node *new_node;
    size_t want_size = 0;;
    void *return_ptr = NULL;

    if (_want_size == 0) {
        return NULL;
    }

    want_size = (_want_size + heapStructSize + ALIGNMENT_MASK) & ~(size_t)ALIGNMENT_MASK;

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
static void InsertFreeBlock(heap_node* _insert_block_ptr);
void Heap_Free(void *_free_ptr) {
    heap_node *link_ptr;
    uint8_t *free_ptr = (uint8_t *)_free_ptr;

    free_ptr -= heapStructSize;
    link_ptr = (heap_node *)free_ptr;
    
    theHeap.allSize += link_ptr->blockSize;
    InsertFreeBlock(link_ptr);
}

static void InsertFreeBlock(heap_node* _insert_block_ptr) {
    if (_insert_block_ptr == NULL) {
        return;
    }
    heap_node *first_fit_node = NULL;
    heap_node *insert_block_ptr = _insert_block_ptr;
    uint8_t *get_addr = NULL;


    for (first_fit_node = &theHeap.head; first_fit_node->next != theHeap.tail && first_fit_node->next < insert_block_ptr; first_fit_node = first_fit_node->next) {
        // finding the fit node
    }

    insert_block_ptr->next = first_fit_node->next;
    first_fit_node->next = insert_block_ptr; 

    get_addr = (uint8_t *)insert_block_ptr;
    if ((get_addr + insert_block_ptr->blockSize) == (uint8_t *)(insert_block_ptr->next)) {
        if (insert_block_ptr->next != theHeap.tail) {
            insert_block_ptr->blockSize += insert_block_ptr->next->blockSize;
            insert_block_ptr->next = insert_block_ptr->next->next;
        } else {
            insert_block_ptr->next = theHeap.tail;
        }
    }
    get_addr = (uint8_t *)first_fit_node;
    if (get_addr + first_fit_node->blockSize == (uint8_t *)insert_block_ptr) {
        first_fit_node->blockSize += insert_block_ptr->blockSize;
        first_fit_node->next = insert_block_ptr->next;
    }
}

Class (Stack_Register) {
        //manual stacking
    uint32_t r4;
    uint32_t r5;
    uint32_t r6;
    uint32_t r7;
    uint32_t r8;
    uint32_t r9;
    uint32_t r10;
    uint32_t r11;
    //automatic stacking
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r12;
    uint32_t LR;
    uint32_t PC;
    uint32_t xPSR;
};


Class (TCB_t) {
    volatile uint32_t *top_of_stack;
    unsigned long priority;
    uint32_t *stack;
    Stack_Register *self_stack;  //Save the status of the stack in the task !You can use gdb to debug it!
};

typedef  TCB_t         *TaskHandle_t;
__attribute__( ( used ) )  TCB_t * volatile currentTCB = NULL;
typedef void (* TaskFunction_t)( void * );

uint32_t *PortInitialiseStack(uint32_t *_topOfStack, 
                              TaskFunction_t code, 
                              void *parameters, 
                              TaskHandle_t *const self
    ) {
    uint32_t *top_of_stack = _topOfStack - 16;
    Stack_Register *stack = (Stack_Register *)top_of_stack;

    stack->xPSR = 0x01000000UL;
    stack->PC = ((uint32_t)code) & ((uint32_t)0xfffffffeUL);
    stack->LR = (uint32_t)parameters;
    stack->r0 = (uint32_t)self;
    (*self)->self_stack = stack;

    return top_of_stack;
}
void TaskCreate(TaskFunction_t taskCode, uint16_t const stackDepth, 
                void *const parameters,
                uint32_t _priority, 
                TaskHandle_t *const self
    ) {
    uint32_t *top_stack = NULL;
    TCB_t *new_tcb = (TCB_t *)Heap_Malloc(sizeof(TCB_t));
    *self = (TCB_t *)new_tcb;
    
    new_tcb->priority = _priority;
    new_tcb->stack = (uint32_t *)Heap_Malloc((size_t)stackDepth * sizeof(uint32_t));
    
    top_stack = new_tcb->stack + (stackDepth - (uint32_t)1);
    top_stack = (uint32_t *)((uint32_t)top_stack & ~(uint32_t)ALIGNMENT_MASK);
    new_tcb->top_of_stack = PortInitialiseStack(top_stack, taskCode, parameters, self);

    currentTCB = new_tcb;
}

TaskHandle_t leisureTcb = NULL;

void EnterSleepMode(void) {

    ws2812.SetPixelRGB(&ws2812, 0, 255, 0, 255);
    ws2812.Show(&ws2812);
    HAL_Delay(500);
    ws2812.SetPixelRGB(&ws2812, 0, 0, 0, 255);
    ws2812.Show(&ws2812);
    HAL_Delay(500);

}

void leisureTask() {
    while (1) {
        EnterSleepMode();
    }
}

void SchedulerInit(void) {
    TaskCreate(leisureTask,
               128,
               NULL,
               0,
               &leisureTcb
    );
}

__attribute__( ( always_inline ) ) inline void SchedulerStart( void )
{
    /* Start the first task. */
    __asm volatile (
            " ldr r0, =0xE000ED08 	\n"/* Use the NVIC offset register to locate the stack. */
            " ldr r0, [r0] 			\n"
            " ldr r0, [r0] 			\n"
            " msr msp, r0			\n"/* Set the msp back to the start of the stack. */
            " cpsie i				\n"/* Globally enable interrupts. */
            " cpsie f				\n"
            " dsb					\n"
            " isb					\n"
            " svc 0					\n"/* System call to start first task. */
            " nop					\n"
            " .ltorg				\n"
            );
}