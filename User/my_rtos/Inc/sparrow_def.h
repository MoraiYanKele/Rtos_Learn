#pragma once

#include <stdint.h>

/* ==========================================================================
 * 内核公共类型定义。只放类型，不放函数声明，也不依赖任何架构头文件。
 * ========================================================================== */

/* 一次写完 "typedef + struct" 的语法糖：
 *     Class(foo) { int a; };
 * 展开成
 *     typedef struct foo foo;
 *     struct foo { int a; };
 */
#define Class(class)            \
    typedef struct class class; \
    struct class

/* 任务栈上的寄存器快照。具体布局由架构决定，定义在 port/port_def.h 里；
 * 这里只做前置声明，让 TCB_t 能持有一个指向它的指针。
 * （C11 允许同一个 typedef 名重复声明为相同类型，所以两处并不冲突。） */
typedef struct Stack_Register Stack_Register;

/* 任务控制块。
 *
 * 注意：top_of_stack 必须是第一个成员。SVC / PendSV 的汇编里直接用
 *       ldr r0, [r1] 读偏移 0 处的栈顶指针，成员顺序不能调整。 */
Class (TCB_t) {
    volatile uint32_t *top_of_stack;
    unsigned long priority;
    uint32_t *stack;
    Stack_Register *self_stack;  //Save the status of the stack in the task !You can use gdb to debug it!
};

typedef  TCB_t         *TaskHandle_t;
typedef void (* TaskFunction_t)( void * );
