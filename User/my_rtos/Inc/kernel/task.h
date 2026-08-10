#pragma once

#include <stdint.h>

#include "sparrow_conf.h"
#include "sparrow_def.h"

/* 当前正在运行的任务。
 *
 * SVC / PendSV 的汇编用字面量池 ".word currentTCB" 直接引用这个符号，
 * 所以它必须保持外部链接（不能改成 static），定义处的 used 属性也不能去掉。 */
extern TCB_t * volatile currentTCB;

/* 优先级 -> 任务句柄。当前实现里一个优先级只放一个任务。 */
extern TaskHandle_t tcbTaskTable[CONFIG_MAX_PRIORI];

/* 创建任务。
 *   stackDepth  栈深度，单位是字（uint32_t），不是字节
 *   priority    优先级，数字越大越高，0 已被空闲任务占用
 *   self        出参，返回任务句柄 */
void TaskCreate(TaskFunction_t taskCode, uint16_t const stackDepth,
                void *const parameters,
                uint32_t priority,
                TaskHandle_t *const self);
