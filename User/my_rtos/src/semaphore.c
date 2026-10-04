#include "semaphore.h"
#include "heap.h"
#include "schedule.h"

Semaphore_t *SemaphoreCreate(uint8_t value) {

    Semaphore_t *semaphore = Heap_Malloc(sizeof(Semaphore_t));
    if (semaphore == NULL) {
        return NULL;
    }
    semaphore->blockTable = 0;
    semaphore->value = value;

    return semaphore;
}

uint8_t SemaphoreDelete(Semaphore_t *semaphore) {
    if (semaphore == NULL) {
        return false;
    }
    uint32_t basepri = EnterCritical();
    if (semaphore->blockTable != 0) {
        ExitCritical(basepri);
        return false;
    }
    Heap_Free(semaphore);
    ExitCritical(basepri);
    return true;
}

#define FindTopTcbIndex FindHighestPriority

uint8_t SemaphoreRelease(Semaphore_t *semaphore) {
    if (semaphore == NULL) {
        return false;
    }

    uint32_t basepri = EnterCritical();

    if (semaphore->blockTable != 0) {
        uint8_t priority = FindHighestPriority(semaphore->blockTable);
        TaskHandle_t task = GetTaskHandle(priority);
        uint32_t curr_prio = GetCurrentTCB()->priority;
        TaskCompleteWaitLocked(task, WAIT_SIGNALED);

        if (priority > curr_prio) {
            SwitchTask();
        }
    } else {
        semaphore->value++;
    }
    
    ExitCritical(basepri);
    return true;
}

uint8_t SemaphoreTake(Semaphore_t *semaphore, uint32_t ticks) {
    if (semaphore == NULL) {
        return false;
    }

    uint32_t basepri = EnterCritical();

    if (semaphore->value > 0) {
        semaphore->value -= 1;
        ExitCritical(basepri);
        return true;
    }

    if (ticks == 0) {
        ExitCritical(basepri);
        return false;
    }

    TaskHandle_t task = GetCurrentTCB();
    uint8_t curr_prio = task->priority;

    task->waitTable = &semaphore->blockTable;
    task->waitResult = WAIT_PENDING;

    StateAdd(task, task->waitTable);
    StateAdd(task, &stateTable[BLOCK]);
    StateAdd(task, &stateTable[DELAY]);
    wakeTicksTable[curr_prio] = tickBase + ticks;
    
    StateRemove(task, &stateTable[READY]);

    SwitchTask();
    ExitCritical(basepri);

    basepri = EnterCritical();
    WaitResult_t result = task->waitResult;
    task->waitResult = WAIT_NONE;

    if (result == WAIT_TIMEOUT) {
        ExitCritical(basepri);
        return false;
    } else {
        ExitCritical(basepri);
        return true;
    }

}