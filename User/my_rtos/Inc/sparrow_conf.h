#pragma once

#include <stdint.h>

/* ==========================================================================
 * sparrow 内核可调参数
 *
 * 只有这个文件里的宏是"按需要改"的，其它头文件不要再放配置项。
 * ========================================================================== */

/* 堆总大小（字节）。TCB 和任务栈都从这块静态内存里分配。 */
#define CONFIG_HEAP                     (8 * 1024)

/* 支持的优先级数量。数字越大优先级越高，优先级 0 留给空闲任务。
 * 就绪状态用一个 uint32_t 位图表示，所以这个值不能超过 32。 */
#define CONFIG_MAX_PRIORI               32

/* 临界区里写进 basepri 的值。191 = 0xBF，屏蔽优先级数值 >= 0xB0 的中断；
 * 数值更小（更紧急）的中断不受临界区影响，也因此不能调用内核 API。 */
#define CONFIG_SHIELD_INTER_PRIORITY    191

/* SysTick 的时钟源频率，等于 HCLK。 */
#define CONFIG_SYSTICK_CLOCK_HZ         ((unsigned long) 168000000)

/* 系统节拍频率。1000Hz 即 1 个 tick = 1ms。 */
#define CONFIG_TICK_RATE_HZ             ((uint32_t) 1000)
