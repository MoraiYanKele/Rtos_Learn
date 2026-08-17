#include <stdint.h>
#include <stdlib.h>

#include "sparrow.h"
#include "ws2812.h"
#include "main.h"
#include "VOFA.h"

#define true    1
#define false   0

#define CONFIG_HEAP                     8 * 1024
#define ALIGNMENT_MASK                  (uintptr_t)0x07

#define MIN_SIZE                        ((size_t)(heapStructSize << 1))

#define CONFIG_MAX_PRIORI               32

#define CONFIG_MAX_SYSCALL_PRIORITY     11U
#define CONFIG_SHIELD_INTER_PRIORITY    (CONFIG_MAX_SYSCALL_PRIORITY << (8U - __NVIC_PRIO_BITS))

#define TICK_HALF_RANGE                 0x80000000UL

#define Class(class)            \
    typedef struct class class; \
    struct class


// #define SwitchTask() \
// *( ( volatile uint32_t * ) 0xe000ed04 ) = ( 1UL << 28UL );

__attribute__((always_inline))
static inline void SwitchTask(void) {
    SCB->ICSR = SCB_ICSR_PENDSVSET_Msk;

    __DSB();
    __ISB();
}


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

uint32_t readyBitTable = 0; // 就绪表
uint32_t delayBitTable = 0; // 延时表
uint32_t suspendTable = 0;  // 挂起表
uint32_t deadBitTable = 0;  // 死亡表
uint32_t blockTable = 0;    // 阻塞表


uint32_t nextTicks = ~(uint32_t)0;
uint32_t tickBase = 0;


uint32_t wakeTicksTable[CONFIG_MAX_PRIORI] = {0}; // 任务唤醒时间表

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
    if (_free_ptr == NULL) {
        return;
    }
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


__attribute__( ( used ) )  TCB_t * volatile currentTCB = NULL;
TaskHandle_t tcbTaskTable[CONFIG_MAX_PRIORI] = { NULL };

uint32_t *PortInitialiseStack(uint32_t *_topOfStack, 
                              TaskFunction_t code, 
                              void *parameters, 
                              TaskHandle_t *const self
    ) {
    uint32_t *top_of_stack = _topOfStack - 16;
    Stack_Register *stack = (Stack_Register *)top_of_stack;

    stack->xPSR = 0x01000000UL;
    stack->PC = ((uint32_t)code) & ((uint32_t)0xfffffffeUL);
    stack->LR = (uint32_t)parameters; // 这里的parameters 和 self 均做调试用，与正常rtos中的不同
    stack->r0 = (uint32_t)self;
    (*self)->self_stack = stack;

    return top_of_stack;
}
void TaskCreate(TaskFunction_t taskCode, uint16_t const stackDepth, 
                void *const parameters,
                uint32_t _priority, 
                TaskHandle_t *const self
    ) {
    if (_priority >= CONFIG_MAX_PRIORI) {
        return;
    }
    uint32_t *top_stack = NULL;
    TCB_t *new_tcb = (TCB_t *)Heap_Malloc(sizeof(TCB_t));
    *self = (TCB_t *)new_tcb;

    tcbTaskTable[_priority] = new_tcb;
    
    new_tcb->priority = _priority;
    new_tcb->stack = (uint32_t *)Heap_Malloc((size_t)stackDepth * sizeof(uint32_t));
    
    top_stack = new_tcb->stack + (stackDepth - (uint32_t)1);
    top_stack = (uint32_t *)((uint32_t)top_stack & ~(uint32_t)ALIGNMENT_MASK);
    new_tcb->top_of_stack = PortInitialiseStack(top_stack, taskCode, parameters, self);

    currentTCB = new_tcb;

    readyBitTable |= (1UL << _priority);
}

TaskHandle_t leisureTcb = NULL;

void EnterSleepMode(void) {
    SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
    __WFI();
}

void leisureTask(void *parameters) {
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

void SysTick_Handler(void) {
    uint32_t basepri = EnterCritical();

    uint32_t temp = suspendTable;
    temp &= 1;
    if (temp != 1) {
        CheckTicks();
    }

    ExitCritical(basepri);
}




__attribute__((always_inline)) static inline uint8_t FindHighestPriority(uint32_t table) {
    uint8_t top_zero_number;
    uint8_t temp;

    __asm volatile (
        "clz %0, %2\n"
        "mov %1, #31\n"
        "sub %0, %1, %0\n"
        :"=r" (top_zero_number),"=r"(temp)
        :"r" (table)
    );
    return top_zero_number;
}

void TaskSwitchContext(void) {

    if(readyBitTable == 0) {
        return;
    }

    uint8_t highest_priority = FindHighestPriority(readyBitTable);
    currentTCB = tcbTaskTable[highest_priority];
}


static inline uint8_t IsTickReached(uint32_t now, uint32_t deadline) {
    return (uint32_t)(now - deadline) < TICK_HALF_RANGE;
}

void TaskDelay(uint16_t _ticks) {

    if (_ticks == 0) {
        SwitchTask();
        return;
    }

    uint32_t basepri = EnterCritical();

    uint32_t priority = currentTCB->priority;
    uint32_t task_bit = 1UL << priority;

    wakeTicksTable[priority] = tickBase + (uint32_t)_ticks;
    
    delayBitTable |= task_bit;
    readyBitTable &= ~task_bit;

    SwitchTask();
    ExitCritical(basepri);   
}

// static void TaskDelayLocked(TCB_t *task, uint32_t ticks) {
//     uint32_t piorityBit = 1UL << task->priority;
//     uint32_t wakeTime = tickBase + ()
// }

void CheckTicks(void) {
    tickBase += 1;

    uint32_t lookup_table = delayBitTable;

    while (lookup_table != 0) {
        uint8_t i = FindHighestPriority(lookup_table);
        uint32_t task_bit = 1UL << i;

        lookup_table &= ~task_bit;

        if (IsTickReached(tickBase, wakeTicksTable[i])) {
            wakeTicksTable[i] = 0;

            delayBitTable &= ~task_bit;
            readyBitTable |= task_bit;
        }
    }
    SwitchTask();
}


#define vPortSVCHandler SVC_Handler
#define xPortPendSVHandler PendSV_Handler


__attribute__((naked)) void vPortSVCHandler(void) {
    __asm volatile (
            " ldr r3, pxCurrentTCBConst2 	\n"
            " ldr r1, [r3] 			\n"
            " ldr r0, [r1] 			\n"/* Copy the top of stack from the TCB. */
            " ldmia r0!, {r4-r11} 	\n"/* Pop the registers that are not automatically saved on exception entry and the critical nesting count. */
            " msr psp, r0			\n"/* Restore the task stack pointer. */
            "isb                        \n"
            " mov r0, #0			\n"
            " msr basepri, r0		\n"/* Clear the base priority register. */
            " orr r14, #0xd			\n"/* Return from interrupt uses the PSP. */
            " bx r14					\n"
            ".align 4				\n"
            "pxCurrentTCBConst2: .word currentTCB   \n"
            );
}

__attribute__( ( naked ) )  void  xPortPendSVHandler( void ) {
    __asm volatile (
        "	mrs r0, psp							\n"
        "	isb									\n"
        "										\n"
        "	ldr	r3, pxCurrentTCBConst			\n"
        "	ldr	r2, [r3]						\n"
        "										\n"
        "	stmdb r0!, {r4-r11}					\n"
        "	str r0, [r2]						\n"
        "										\n"
        "	stmdb sp!, {r3, r14}				\n"
        "	mov r0, %0							\n"
        "	msr basepri, r0						\n"
        "   dsb                                 \n"
        "   isb                                 \n"
        "	bl TaskSwitchContext				\n"
        "	mov r0, #0							\n"
        "	msr basepri, r0						\n"
        "	ldmia sp!, {r3, r14}				\n"
        "										\n"
        "	ldr r1, [r3]						\n"
        "	ldr r0, [r1]						\n"
        "	ldmia r0!, {r4-r11}					\n"
        "	msr psp, r0							\n"
        "	isb									\n"
        "	bx r14								\n"
        "	nop									\n"
        "	.align 4							\n"
        "pxCurrentTCBConst: .word currentTCB	\n"
        ::"i" ( CONFIG_SHIELD_INTER_PRIORITY )
    );
}

__attribute__( ( always_inline ) ) inline void SchedulerStart( void )
{
    ( *( ( volatile uint32_t * ) 0xe000ed20 ) ) |= ( ( ( uint32_t ) 255UL ) << 16UL );
    ( *( ( volatile uint32_t * ) 0xe000ed20 ) ) |= ( ( ( uint32_t ) 255UL ) << 24UL );

    SysTick->CTRL = 0UL;
    SysTick->VAL = 0UL;
    /* Configure SysTick to interrupt at the requested rate. */
    SysTick->LOAD = ( CONFIG_SYSTICK_CLOCK_HZ / CONFIG_TICK_RATE_HZ ) - 1UL;
    SysTick->CTRL = ( ( 1UL << 2UL ) | ( 1UL << 1UL ) | ( 1UL << 0UL ) );
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

__attribute__( ( always_inline ) ) 
static inline uint32_t EnterCritical(void) {
    uint32_t old_basepri;
    uint32_t new_basepri = CONFIG_SHIELD_INTER_PRIORITY;

    __asm volatile(
        "mrs %0, basepri       \n"
        "msr basepri_max, %1   \n"
        "dsb                   \n"
        "isb                   \n"
        : "=r"(old_basepri)
        : "r"(new_basepri)
        : "memory"
    );

    return old_basepri;
} 

__attribute__((always_inline))
static inline void ExitCritical(uint32_t old_basepri) {
    __asm volatile(
        "msr basepri, %0       \n"
        "dsb                   \n"
        "isb                   \n"
        :
        : "r"(old_basepri)
        : "memory"
    );
}


uint32_t StateAdd(TCB_t *self, uint32_t *stateTable) {
    uint32_t old_basepri = EnterCritical();
    (*stateTable) |= (1UL << self->priority);
    ExitCritical(old_basepri);
    return *stateTable;
}

uint32_t StateRemove(TCB_t *self, uint32_t *stateTable) {
    uint32_t old_basepri = EnterCritical();
    (*stateTable) &= ~(1UL << self->priority);
    ExitCritical(old_basepri);
    return *stateTable;
}

uint8_t CheckState(TCB_t *self, uint32_t *stateTable) {
    uint32_t old_basepri = EnterCritical();
    uint32_t state = ((*stateTable) & (1UL << self->priority)) != 0;
    ExitCritical(old_basepri);
    return (uint8_t)state;
}

Class (Semaphore_t) {
    uint8_t value;
    uint32_t block;
};



Semaphore_t *SemaphoreCreate(uint8_t _value) {
    Semaphore_t *semphore = Heap_Malloc(sizeof(Semaphore_t));
    semphore->block = 0;
    semphore->value = _value;
    return semphore;
}

void SemaphoreDelete(Semaphore_t *semaphore) {
    Heap_Free(semaphore);
}

uint8_t SemaphoreRelease(Semaphore_t *semaphore) {
    uint32_t old_basepri = EnterCritical();
    
    if (semaphore->block) {
        uint8_t i = FindHighestPriority(semaphore->block);
        semaphore->block &= ~(1UL << i);
        StateRemove(tcbTaskTable[i], &blockTable);
        StateRemove(tcbTaskTable[i], &delayBitTable);
        wakeTicksTable[i] = 0;
        StateAdd(tcbTaskTable[i], &readyBitTable);
    } else {
        semaphore->value += 1;
    }
    SwitchTask();

    ExitCritical(old_basepri);
    return true;
}


uint8_t SemaphoreTake(Semaphore_t *semaphore, uint32_t ticks) {
    uint32_t old_basepri = EnterCritical();

    if (semaphore == NULL) {
        ExitCritical(old_basepri);
        return false;
    }

    if (semaphore->value > 0) {
        semaphore->value -= 1;
        ExitCritical(old_basepri);
        return true;
    }

    if (ticks == 0) {
        ExitCritical(old_basepri);
        return false;
    }

    TCB_t *task = currentTCB;
    StateAdd(task, &blockTable);
    StateAdd(task, &semaphore->block);

    wakeTicksTable[task->priority] = tickBase + ticks;
    StateAdd(task, &delayBitTable);

    StateRemove(task, &readyBitTable);
    SwitchTask();
    ExitCritical(old_basepri);
    
    old_basepri = EnterCritical();
    if (CheckState(task, &blockTable)) {
        StateRemove(task, &blockTable);
        StateRemove(task, &semaphore->block);
        ExitCritical(old_basepri);
        return false;
    }
    ExitCritical(old_basepri);
    return true;
}