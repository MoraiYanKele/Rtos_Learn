#include <stdint.h>

#include "kernel/sched.h"
#include "kernel/task.h"
#include "kernel/tick.h"
#include "port/port.h"
#include "sparrow_conf.h"

uint32_t readyBitTable = 0;

/* 用 CLZ 找出就绪位图里最高的置位：CLZ 数的是前导零个数，
 * 31 - clz(bitmap) 就是最高置位的位号，也就是最高就绪优先级。
 * 位图为空时没有"最高位"可言，直接返回 0xff 表示无就绪任务
 * （不判空的话 clz(0) 得 31 - 32，会溢出成 255）。 */
__attribute__((always_inline)) static inline uint8_t FindHighestPriority(uint32_t table) {
    if (table == 0) { // No ready tasks
        return 0xff;
    }
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

/* 在 PendSV 里被调用：挑出最高优先级的就绪任务，让 currentTCB 指过去。
 * 真正的寄存器保存和恢复在 PendSV 汇编里完成，这里只负责"选谁"。 */
void TaskSwitchContext(void) {
    uint8_t highest_priority = FindHighestPriority(readyBitTable);
    currentTCB = tcbTaskTable[highest_priority];
}

TaskHandle_t leisureTcb = NULL;

/* 空闲任务：没有别的任务就绪时运行，进睡眠等中断。
 * 它占用优先级 0 且永远保持就绪，保证就绪位图不会为空。 */
void LeisureTask(void *parameters) {
    while (1) {
        EnterSleepMode();
    }
}

void SchedulerInit(void) {
    TickInit();

    TaskCreate(LeisureTask,
               128,
               NULL,
               0,
               &leisureTcb
    );
}
