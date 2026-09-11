#pragma once

#include "stm32f4xx.h"
#include "stm32f4xx_hal.h"
#include "main.h"
#include "config.h"
#include <stdint.h>
#include <stdlib.h> 

#define CONFIG_MAX_PRIORI           32
#define CONFIG_MAX_SYSCALL_PRIORITY     11U
#define CONFIG_SHIELD_INTER_PRIORITY \
        (CONFIG_MAX_SYSCALL_PRIORITY << (8U << __NVIC_PRIO_BITS))

#define ALIGNMENT_MASK              (uintptr_t)0x07

enum State {
    READY   = 0, // 就绪表
    DELAY   = 1, // 延时表
    SUSPEND = 2, // 挂起表
    DEAD    = 3, // 死亡表
    BLOCK   = 4  // 阻塞表
};

#define Class(class)            \
    typedef struct class class; \
    struct class


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
    Stack_Register *self_stack;
};

typedef TCB_t *TaskHandle_t;
typedef void (* TaskFunction_t)(void *);


__attribute__((always_inline)) inline uint8_t FindHighestPriority(uint32_t table) {
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

TaskHandle_t GetTaskHandle(uint8_t priority); 
void SchedulerInit(void);
void SchedulerStart(void);
void TaskCreate(TaskFunction_t taskCode, uint16_t const stackDepth, 
                void *const parameters,
                uint32_t _priority, 
                TaskHandle_t *const self);
                
void TaskDelay(uint16_t _ticks);
void CheckTicks(void);