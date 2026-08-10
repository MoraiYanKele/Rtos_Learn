#include <stdint.h>

#include "kernel/sched.h"
#include "kernel/task.h"
#include "kernel/tick.h"
#include "port/port.h"
#include "port/port_def.h"
#include "sparrow_conf.h"

/* ==========================================================================
 * Cortex-M4 移植层
 *
 * 这个文件是唯一碰架构的地方：栈帧布局、SVC / PendSV / SysTick 三个异常、
 * basepri 临界区、SysTick 配置。换平台时只需要重写这一个文件。
 *
 * 关于下面几个 always_inline 函数的写法，见 Inc/port/port.h 的说明 ——
 * 定义带 inline、头文件里的原型不带，两者配合才会生成可供别的 .c 链接的
 * 外部定义。不要改成 static inline。
 * ========================================================================== */

/* ---------------------------------------------------------------- 栈帧初始化 */

/* 在任务栈顶伪造一个"刚被中断打断"的现场，这样第一次切过去时，
 * 异常返回的硬件动作就会把 PC 弹成任务入口，任务从此开始运行。
 *
 * 16 个字对应 Stack_Register 的全部成员：手动压栈的 r4-r11 在低地址，
 * 硬件自动压栈的 r0-r3/r12/LR/PC/xPSR 在高地址。 */
uint32_t *PortInitialiseStack(uint32_t *stack_top,
                              TaskFunction_t code,
                              void *parameters,
                              TaskHandle_t *const self
    ) {
    uint32_t *top_of_stack = stack_top - 16;
    Stack_Register *stack = (Stack_Register *)top_of_stack;

    stack->xPSR = 0x01000000UL;                                 /* Thumb 位置 1 */
    stack->PC = ((uint32_t)code) & ((uint32_t)0xfffffffeUL);     /* 入口地址，清掉最低位 */
    stack->LR = (uint32_t)parameters;
    stack->r0 = (uint32_t)self;

    /* 把栈帧地址记在 TCB 里，调试时可以直接看这个任务保存的寄存器现场。 */
    (*self)->self_stack = stack;

    return top_of_stack;
}

/* -------------------------------------------------------------------- 临界区 */

/* 抬高 basepri 屏蔽掉普通优先级的中断，返回原值供 ExitCritical 还原。
 * 改 basepri 前后关一次总中断，是为了让"读旧值 + 写新值"不被打断。 */
__attribute__( ( always_inline ) ) inline uint32_t EnterCritical(void) {
    uint32_t return_value;
    uint32_t temp = 0;

    __asm volatile(
        "cpsid i			\n"
        "mrs %0, basepri	\n"
        "mov %1, %2			\n"
        "msr basepri, %1	\n"
        "dsb                \n"
        "isb                \n"
        "cpsie i			\n"
        : "=r"(return_value), "=r"(temp)
        : "i"(CONFIG_SHIELD_INTER_PRIORITY)
        : "memory"
    );

    return return_value;
}

__attribute__( ( always_inline ) ) inline void ExitCritical(uint32_t basepri) {
    __asm volatile(
        "cpsid i			\n"
        "msr basepri, %0	\n"
        "dsb                \n"
        "isb                \n"
        "cpsie i			\n"
        :
        : "r"(basepri)
        : "memory"
    );
}

/* ---------------------------------------------------------------------- 低功耗 */

void EnterSleepMode(void) {
    /* 清掉 SLEEPDEEP，用普通睡眠，中断能立刻把核唤醒。 */
    SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
    __WFI();
}

/* ------------------------------------------------------------------ 上下文切换 */

/* SVC 只用一次：启动第一个任务。此时没有"上一个任务"需要保存，
 * 所以只做恢复：从 currentTCB 取栈顶，弹出 r4-r11，剩下的由异常返回补齐。 */
__attribute__((naked)) void SVC_Handler(void) {
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

/* 真正的上下文切换：把当前任务剩下的寄存器压进它自己的栈、更新 TCB 栈顶，
 * 调 TaskSwitchContext 选出新任务，再按同样的方式把新任务的现场恢复回来。 */
__attribute__( ( naked ) )  void  PendSV_Handler( void ) {
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

/* -------------------------------------------------------------------- 系统节拍 */

/* 覆盖 Core/Src/stm32f4xx_it.c 里的 weak 版本。
 * SysTick 能被内核征用，是因为 HAL 的时基改用了 TIM1
 * （见 Core/Src/stm32f4xx_hal_timebase_tim.c）。 */
void SysTick_Handler(void) {
    uint32_t basepri = EnterCritical();
    CheckTicks();
    ExitCritical(basepri);
}

/* ---------------------------------------------------------------------- 启动 */

__attribute__( ( always_inline ) ) inline void SchedulerStart( void )
{
    /* SHPR3：把 PendSV 和 SysTick 都设成最低优先级，保证它们不会打断
     * 任何普通中断，上下文切换总在最后才发生。 */
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
