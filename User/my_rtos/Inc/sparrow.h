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

//  触发PendSV中断
#define switchTask() \ 
*( ( volatile uint32_t * ) 0xe000ed04 ) = ( 1UL << 28UL );

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
typedef void (* TaskFunction_t)( void * );

void SchedulerInit(void);
void SchedulerStart(void);
void TaskCreate(TaskFunction_t taskCode, uint16_t const stackDepth, 
                void *const parameters,
                uint32_t _priority, 
                TaskHandle_t *const self);
                
void ExitCritical(uint32_t _basepri);
uint32_t EnterCritical(void);