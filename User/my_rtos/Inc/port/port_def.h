#pragma once

#include <stdint.h>

#include "sparrow_def.h"

/* CMSIS 设备头。工程全局定义了 USE_HAL_DRIVER，stm32f4xx.h 末尾会自己把
 * stm32f4xx_hal.h 带进来，所以内核这边不需要再显式 include HAL。 */
#include "stm32f4xx.h"

/* ==========================================================================
 * Cortex-M4 架构相关定义
 * ========================================================================== */

/* AAPCS 要求栈按 8 字节对齐，堆块也沿用同一个对齐粒度。
 * 用作掩码：向上对齐 (x + MASK) & ~MASK，向下对齐 x & ~MASK。 */
#define ALIGNMENT_MASK      (uintptr_t)0x07

/* 触发 PendSV 中断，请求一次上下文切换。
 * 0xe000ed04 是 SCB->ICSR，bit28 = PENDSVSET。
 * 真正的切换发生在 PendSV 里，而不是这条语句执行的位置。 */
#define SwitchTask()                                                \
    do {                                                            \
        *( ( volatile uint32_t * ) 0xe000ed04 ) = ( 1UL << 28UL );   \
    } while (0)

/* 一次异常进出栈上保存的完整寄存器组，顺序就是内存里的实际排列：
 * 低地址一半由 PendSV 汇编手动压栈，高地址一半由硬件自动压栈。 */
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
