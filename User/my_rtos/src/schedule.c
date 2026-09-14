#include "schedule.h"

static void TaskExitError(void) {
    __disable_irq();
    Error_Handler();
}

uint32_t nextTicks = ~(uint32_t)0;
uint32_t tickBase = 0;

TaskHandle_t tcbTaskTable[CONFIG_MAX_PRIORI] = { NULL };
uint32_t wakeTicksTable[CONFIG_MAX_PRIORI] = {0}; // 任务唤醒时间表
uint32_t stateTable[5] = {0, 0, 0, 0, 0};

static TaskHandle_t taskTcbTable[CONFIG_MAX_PRIORI] = {NULL};
static TaskHandle_t leisureTcb = NULL;
__attribute__((used)) TCB_t * volatile currentTCB = NULL;


__attribute__((always_inline))
static inline void SwitchTask(void) {
    SCB->ICSR = SCB_ICSR_PENDSVSET_Msk;

    __DSB();
    __ISB();
}


__attribute__((always_inline))
static inline uint32_t EnterCritical(void) {
    uint32_t old_basepri;
    uint32_t new_basepri = CONFIG_SHIELD_INTER_PRIORITY;

    __asm volatile(
        "mrs %0, basepri       \n"
        "msr basepri_max, %1   \n"
        "dsb                   \n"
        "isb                   \n"
        : "=&r"(old_basepri)
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

static uint32_t *PortInitialiseStack(uint32_t *topOfStack, 
                                     TaskFunction_t code, 
                                     void *parameters,
                                     TaskHandle_t *const self
) {
    uint32_t *top_of_stack = topOfStack - (sizeof(Stack_Register) / sizeof(uint32_t));
    Stack_Register *stack = (Stack_Register *)top_of_stack;

    stack->xPSR = 0x01000000UL;
    stack->xPSR = 0x01000000UL;
    stack->PC = ((uint32_t)code) & ((uint32_t)0xfffffffeUL);
    stack->LR = (uint32_t)TaskExitError;
    stack->r0 = (uint32_t)parameters;
    (*self)->self_stack = stack;
    return top_of_stack; // 
}

void TaskCreate(TaskFunction_t taskCode, uint16_t const stackDepth, 
                void *const parameters,
                uint32_t priority, 
                TaskHandle_t *const self
    ) {
    if (priority >= CONFIG_MAX_PRIORI) {
        return;
    }
    if (self == NULL) {
        return;
    }
    if (taskCode == NULL) {
        return;
    }
    uint32_t *top_stack = NULL;
    TCB_t *new_tcb = (TCB_t *)Heap_Malloc(sizeof(TCB_t));
    *self = (TCB_t *)new_tcb;

    taskTcbTable[priority] = new_tcb;
    
    new_tcb->priority = priority;
    new_tcb->stack = (uint32_t *)Heap_Malloc((size_t)stackDepth * sizeof(uint32_t));
    
    top_stack = new_tcb->stack + (stackDepth - (uint32_t)1);
    top_stack = (uint32_t *)((uint32_t)top_stack & ~(uint32_t)ALIGNMENT_MASK);
    new_tcb->top_of_stack = PortInitialiseStack(top_stack, taskCode, parameters, self);

    stateTable[READY] |= (1UL << priority);
}

static void EnterSleepMode(void) {
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

    uint32_t temp = stateTable[SUSPEND];
    temp &= 1;
    if (temp != 1) {
        CheckTicks();
    }

    ExitCritical(basepri);
}

TaskHandle_t GetTaskHandle(uint8_t priority) {
    return taskTcbTable[priority];
}

void TaskSwitchContext(void) {

    if(stateTable[READY] == 0) {
        return;
    }

    uint8_t highest_priority = FindHighestPriority(stateTable[READY]);
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
    
    stateTable[DELAY] |= task_bit;
    stateTable[READY] &= ~task_bit;

    SwitchTask();
    ExitCritical(basepri);   
}

// static void TaskDelayLocked(TCB_t *task, uint32_t ticks) {
//     uint32_t piorityBit = 1UL << task->priority;
//     uint32_t wakeTime = tickBase + ()
// }

void CheckTicks(void) {
    tickBase += 1;

    uint32_t lookup_table = stateTable[DELAY];

    while (lookup_table != 0) {
        uint8_t i = FindHighestPriority(lookup_table);
        uint32_t task_bit = 1UL << i;

        lookup_table &= ~task_bit;

        if (IsTickReached(tickBase, wakeTicksTable[i])) {
            wakeTicksTable[i] = 0;

            stateTable[DELAY] &= ~task_bit;
            stateTable[READY] |= task_bit;
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
        "isb                    \n"
        " mov r0, #0			\n"
        " msr basepri, r0		\n"/* Clear the base priority register. */
        " orr r14, #0xd			\n"/* Return from interrupt uses the PSP. */
        " bx r14			    \n"
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

    TaskSwitchContext(); // 所欲哦初始任务已经创建完成，在这里主动选择最高优先级

    if (currentTCB == NULL) {
        Error_Handler();
    }

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
TaskHandle_t GetCurrentTCB() {
    return currentTCB;
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