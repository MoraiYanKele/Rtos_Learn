#pragma once

#include "stm32f4xx.h"
#include "stm32f4xx_hal.h"
#include "main.h"
#include "config.h"
#include <stdint.h>
#include <stdlib.h> 
#include "heap.h"

#define CONFIG_MAX_PRIORI           32
#define CONFIG_MAX_SYSCALL_PRIORITY     11U
#define CONFIG_SHIELD_INTER_PRIORITY \
        (CONFIG_MAX_SYSCALL_PRIORITY << (8U - __NVIC_PRIO_BITS))

#define ALIGNMENT_MASK              (uintptr_t)0x07

#define TICK_HALF_RANGE                 0x80000000UL

enum State {
    READY   = 0, // 就绪表
    DELAY   = 1, // 延时表
    SUSPEND = 2, // 挂起表
    DEAD    = 3, // 死亡表
    BLOCK   = 4  // 阻塞表
};

typedef enum {
    WAIT_NONE,
    WAIT_PENDING,       // 正在等待
    WAIT_SIGNALED,      // 通知唤醒
    WAIT_TIMEOUT,       // 等待超时
//  可加取消原因
} WaitResult_t;


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
    uint32_t *waitTable; // 当前对象的等待位图
    WaitResult_t waitResult;
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


extern uint32_t tickBase; 
extern uint32_t wakeTicksTable[CONFIG_MAX_PRIORI]; // 任务唤醒时间表
extern uint32_t stateTable[5];

TaskHandle_t GetTaskHandle(uint8_t priority); 
void SchedulerInit(void);
void SchedulerStart(void);
void TaskCreate(TaskFunction_t taskCode, uint16_t const stackDepth, 
                void *const parameters,
                uint32_t _priority, 
                TaskHandle_t *const self);
                
void TaskDelay(uint16_t _ticks);
void CheckTicks(void);
uint32_t StateAdd(TCB_t *self, uint32_t *stateTable);
uint32_t StateRemove(TCB_t *self, uint32_t *stateTable);
uint8_t CheckState(TCB_t *self, uint32_t *stateTable);
TaskHandle_t GetCurrentTCB();
void TaskCompleteWaitLocked(TCB_t *task, WaitResult_t result);
