#include <stdint.h>

#include "kernel/sched.h"
#include "kernel/task.h"
#include "kernel/tick.h"
#include "port/port_def.h"
#include "sparrow_conf.h"

uint32_t tickBase = 0;
uint32_t delayBitTable = 0;

/* 预留位，目前没有人读写它。见 kernel/tick.h 里的说明。 */
uint32_t nextTicks = ~(uint32_t)0;

/* 两张唤醒时刻表的实体。外界只通过下面那对指针访问，所以这两个数组是内部的。 */
static uint32_t ticksTable[CONFIG_MAX_PRIORI] = {0};
static uint32_t tickTableAssist[CONFIG_MAX_PRIORI] = {0};

uint32_t *wakeTicksTable;
uint32_t *overTicksTable;

void TickInit(void) {
    wakeTicksTable = ticksTable;
    overTicksTable = tickTableAssist;
}

/* tickBase 绕回 0 时调用：原来"溢出之后才到期"的那张表，现在成了当前有效的表。 */
static inline void TicksTableSwitch(void) {
    uint32_t *temp = wakeTicksTable;
    wakeTicksTable = overTicksTable;
    overTicksTable = temp;
}

void TaskDelay(uint16_t ticks) { // 也可以设置为 uint32_t 类型，逻辑不变
    uint32_t wake_time = tickBase + ticks;
    TCB_t *current_task = currentTCB;

    /* 加出来比原值还小，说明这次加法回绕了，唤醒时刻落在下一轮，记进溢出表。 */
    if (wake_time < tickBase) {
        overTicksTable[current_task->priority] = wake_time;
    } else {
        wakeTicksTable[current_task->priority] = wake_time;
    }

    delayBitTable |= (1UL << current_task->priority);
    readyBitTable &= ~(1UL << current_task->priority);

    SwitchTask();
}

void CheckTicks(void) {
    tickBase += 1;
    if (tickBase == 0) {
        TicksTableSwitch();
    }

    /* 扫一遍唤醒表，把到期的任务从延时态放回就绪态。
     * 表项为 0 表示该优先级没有在等，所以 0 不算到期。 */
    for (uint8_t i = 0; i < CONFIG_MAX_PRIORI; i++) {
        if (wakeTicksTable[i] > 0 && wakeTicksTable[i] <= tickBase) {
            wakeTicksTable[i] = 0;
            delayBitTable &= ~(1UL << i);
            readyBitTable |= (1UL << i);
        }
    }
    SwitchTask();
}
