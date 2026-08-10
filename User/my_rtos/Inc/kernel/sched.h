#pragma once

#include <stdint.h>

/* ==========================================================================
 * 优先级位图调度器
 *
 * 就绪状态压在一个 uint32_t 里，第 n 位为 1 表示优先级 n 上的任务就绪。
 * 选下一个任务只要找出最高的置位，用 CLZ 一条指令就能做完，是 O(1) 的。
 * ========================================================================== */

extern uint32_t readyBitTable;

/* 初始化时基并创建空闲任务。要在 TaskCreate 之前调用。 */
void SchedulerInit(void);

/* 把 currentTCB 指向当前最高优先级的就绪任务。
 * 由 PendSV 汇编里的 bl 调用，所以必须保持外部链接。 */
void TaskSwitchContext(void);
