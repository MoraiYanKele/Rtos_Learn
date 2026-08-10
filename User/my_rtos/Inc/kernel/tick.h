#pragma once

#include <stdint.h>

/* ==========================================================================
 * 系统时基与任务延时
 *
 * 唤醒时刻用两张表存，配合处理 tickBase 的 32 位溢出：
 * 算出的唤醒时刻如果发生了回绕，就先记进 overTicksTable；等 tickBase 真的
 * 绕回 0，两张表互换身份，原来"溢出后才到期"的那批就变成当前有效的。
 * ========================================================================== */

/* 开机以来的 tick 计数，会溢出回绕。 */
extern uint32_t tickBase;

/* 延时位图：第 n 位为 1 表示优先级 n 上的任务正在延时。 */
extern uint32_t delayBitTable;

/* 预留：打算用来记录"下一个需要唤醒的 tick"，这样 SysTick 里就不用每次扫全表。
 * 参见 Doc/RTOS_OPTIMIZATION_NOTES.md 第 2 节"延时链表优化"。目前未使用。 */
extern uint32_t nextTicks;

/* 当前有效的唤醒时刻表 / 溢出后才有效的那张，两者会互换。 */
extern uint32_t *wakeTicksTable;
extern uint32_t *overTicksTable;

void TickInit(void);

/* 当前任务延时 ticks 个节拍，然后立即请求一次切换。 */
void TaskDelay(uint16_t ticks);

/* 内部函数：只由 SysTick_Handler 调用，负责推进 tickBase 并唤醒到期任务。 */
void CheckTicks(void);
