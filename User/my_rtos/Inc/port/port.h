#pragma once

#include <stdint.h>

#include "port/port_def.h"
#include "sparrow_def.h"

/* ==========================================================================
 * 架构相关接口。实现全部在 src/port/port_cm4.c。
 *
 * 注意 EnterCritical / ExitCritical / SchedulerStart 这三个：
 * 它们的定义带 __attribute__((always_inline)) inline，而这里的原型
 * 故意不带 inline。C11 规则下，只要有一个文件作用域声明不带 inline，
 * 编译器才会真正生成一份外部定义，main.c 里的调用才有符号可链接。
 * 不要给下面的原型加 inline，也不要把定义改成 static inline。
 * ========================================================================== */

/* 在任务栈顶伪造一个异常返回帧，让第一次切换过去就能从 code 开始执行。
 * 返回值是压完帧之后的栈顶，存进 TCB 的 top_of_stack。 */
uint32_t *PortInitialiseStack(uint32_t *stack_top,
                              TaskFunction_t code,
                              void *parameters,
                              TaskHandle_t *const self);

/* 配置 PendSV / SysTick 优先级和节拍，然后用 svc 0 启动第一个任务。不返回。 */
void SchedulerStart(void);

/* 进入临界区，返回原来的 basepri，退出时要原样还回去。 */
uint32_t EnterCritical(void);
void ExitCritical(uint32_t basepri);

/* 空闲任务用：进入睡眠，等任意中断唤醒。 */
void EnterSleepMode(void);
