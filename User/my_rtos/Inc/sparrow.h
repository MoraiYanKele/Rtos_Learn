#pragma once

/* ==========================================================================
 * sparrow —— 一个用于学习的小 RTOS
 *
 * 应用层只需要 include 这一个头文件。
 *
 * 代码结构：
 *   Inc/sparrow_conf.h      可调参数（堆大小、优先级数量、节拍频率……）
 *   Inc/sparrow_def.h       公共类型（TCB_t、TaskHandle_t、TaskFunction_t）
 *   Inc/kernel/heap.h       堆分配器
 *   Inc/kernel/task.h       任务创建与 TCB 表
 *   Inc/kernel/sched.h      优先级位图调度
 *   Inc/kernel/tick.h       时基与任务延时
 *   Inc/port/port_def.h     Cortex-M4 架构定义（栈帧布局、PendSV 触发）
 *   Inc/port/port.h         Cortex-M4 架构接口（栈初始化、临界区、启动）
 *
 * 典型用法：
 *   SchedulerInit();                                  // 建时基和空闲任务
 *   TaskCreate(Task1, 128, NULL, 1, &task1_handle);   // 建自己的任务
 *   SchedulerStart();                                 // 启动，不返回
 * ========================================================================== */

#include <stdint.h>
#include <stdlib.h>

#include "sparrow_conf.h"
#include "sparrow_def.h"

#include "kernel/heap.h"
#include "kernel/sched.h"
#include "kernel/task.h"
#include "kernel/tick.h"

#include "port/port.h"
#include "port/port_def.h"
