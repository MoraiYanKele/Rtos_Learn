#include <stddef.h>
#include <stdint.h>

#include "kernel/heap.h"
#include "kernel/sched.h"
#include "kernel/task.h"
#include "port/port.h"
#include "port/port_def.h"
#include "sparrow_conf.h"

/* used 属性不能去掉：SVC / PendSV 的汇编靠字面量池引用这个符号。 */
__attribute__( ( used ) )  TCB_t * volatile currentTCB = NULL;

TaskHandle_t tcbTaskTable[CONFIG_MAX_PRIORI] = { NULL };

void TaskCreate(TaskFunction_t taskCode, uint16_t const stackDepth,
                void *const parameters,
                uint32_t priority,
                TaskHandle_t *const self
    ) {
    uint32_t *top_stack = NULL;
    TCB_t *new_tcb = (TCB_t *)HeapMalloc(sizeof(TCB_t));
    *self = (TCB_t *)new_tcb;

    tcbTaskTable[priority] = new_tcb;

    new_tcb->priority = priority;
    new_tcb->stack = (uint32_t *)HeapMalloc((size_t)stackDepth * sizeof(uint32_t));

    /* 栈从高地址往低地址生长，所以栈顶取数组末尾，再向下对齐到 8 字节。 */
    top_stack = new_tcb->stack + (stackDepth - (uint32_t)1);
    top_stack = (uint32_t *)((uint32_t)top_stack & ~(uint32_t)ALIGNMENT_MASK);
    new_tcb->top_of_stack = PortInitialiseStack(top_stack, taskCode, parameters, self);

    currentTCB = new_tcb;

    readyBitTable |= (1UL << priority);
}
