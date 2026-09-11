#pragma once

#include "stm32f4xx.h"
#include "stm32f4xx_hal.h"
#include "main.h"
#include <stdint.h>
#include <stdlib.h>


#define Class(class)            \
    typedef struct class class; \
    struct class



#define CONFIG_SYSTICK_CLOCK_HZ         ((unsigned long) 168000000)
#define CONFIG_TICK_RATE_HZ             ((uint32_t) 1000)



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

Class (Semaphore_t) {
    uint8_t value;
    uint32_t block;
};

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

typedef  TCB_t         *TaskHandle_t;
typedef void (* TaskFunction_t)( void * );

void *Heap_Malloc(size_t _want_size);
void Heap_Free(void *_free_ptr); 

TaskHandle_t GetTaskHandle(uint8_t priority); 
void SchedulerInit(void);
void SchedulerStart(void);
void TaskCreate(TaskFunction_t taskCode, uint16_t const stackDepth, 
                void *const parameters,
                uint32_t _priority, 
                TaskHandle_t *const self);
                
void TaskDelay(uint16_t _ticks);
Semaphore_t *SemaphoreCreate(uint8_t _value);
void SemaphoreDelete(Semaphore_t *semaphore);
uint8_t SemaphoreRelease(Semaphore_t *semaphore);
uint8_t SemaphoreTake(Semaphore_t *semaphore, uint32_t ticks);
uint32_t StateRemove(TCB_t *self, uint32_t *stateTable);
uint32_t StateAdd(TCB_t *self, uint32_t *stateTable); 
/* private */ 
void CheckTicks(void);