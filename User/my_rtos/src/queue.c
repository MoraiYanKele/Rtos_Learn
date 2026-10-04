#include "queue.h"
#include "heap.h"
#include "schedule.h"
#include <string.h>

Queue_t *QueueCrate(uint32_t queueLength, uint32_t queueSize) {
    if (queueLength == 0 || queueSize == 0) {
        return NULL;
    }
    size_t q_size = (size_t)(queueLength * queueSize);
    Queue_t *queue = Heap_Malloc(sizeof(Queue_t) + q_size);
    if (queue == NULL) {
        return NULL;
    }
    uint8_t *message_start = (uint8_t *)queue + sizeof(Queue_t);

    *queue = (Queue_t){
        .startPoint = message_start,
        .endPoint = (uint8_t *)(message_start + q_size),
        .readPoint = (uint8_t *)(message_start + (queueLength - 1) * queueSize),
        .writePoint = message_start,
        .messageNumber = 0UL,
        .sendTable = 0UL,
        .receiveTable = 0UL,
        .nodeSize = queueSize,
        .nodeNumber = queueLength,
    };
    return queue;
}

uint8_t QueueDelete(Queue_t *queue) {
    if (queue == NULL) {
        return false;
    }
    uint32_t basepri = EnterCritical();
    if (queue->receiveTable != 0 || queue->sendTable != 0) {
        ExitCritical(basepri);
        return false;
    }
    Heap_Free(queue);
    ExitCritical(basepri);
    return true;
}

#define FindTopTcbIndex FindHighestPriority

// 写入队列
void WriteToQueue(Queue_t *queue, void *buff, uint32_t currentTcbPriority) {
    memcpy((void *)queue->writePoint, buff, (size_t)queue->nodeSize);
    queue->messageNumber++;

    queue->writePoint += queue->nodeSize;
    if (queue->writePoint >= queue->endPoint) {
        queue->writePoint = queue->startPoint;
    }

    if (queue->receiveTable != 0) {
        uint8_t priority = FindTopTcbIndex(queue->receiveTable);
        TaskHandle_t task = GetTaskHandle(priority);

        TaskCompleteWaitLocked(task, WAIT_SIGNALED);

        if (priority > currentTcbPriority) {
            SwitchTask();
        }        
    }
}

// 读取队列
void ExtractFromQueue(Queue_t *queue, void *buff, uint32_t currentTcbPriority) {
    queue->readPoint += queue->nodeSize;
    if (queue->readPoint >= queue->endPoint) {
        queue->readPoint = queue->startPoint;
    }

    memcpy(buff, (void *)queue->readPoint, (size_t)queue->nodeSize);
    queue->messageNumber--;

    if (queue->sendTable != 0) {
        uint8_t priority = FindTopTcbIndex(queue->sendTable);
        TaskHandle_t task = GetTaskHandle(priority);

        TaskCompleteWaitLocked(task, WAIT_SIGNALED);

        if (priority > currentTcbPriority) {
            SwitchTask();
        }
    }

}

uint8_t QueueSend(Queue_t *queue, void *buff, uint32_t ticks) {
    uint32_t basepri = EnterCritical();
    TCB_t *cur_tcb = GetCurrentTCB();
    uint8_t cur_prio = cur_tcb->priority;
    
    if (queue->messageNumber < queue->nodeNumber) {
        WriteToQueue(queue, buff, cur_prio);
        ExitCritical(basepri);
        return true;
    }

    if (ticks == 0) {
        ExitCritical(basepri);
        return false;
    }
    uint32_t end_tick = tickBase + ticks;

    for (;;) {
        
        WaitResult_t wait_res = cur_tcb->waitResult;
        
        if (wait_res == WAIT_TIMEOUT || IsTickReached(tickBase, end_tick)) {
            cur_tcb->waitResult = WAIT_NONE;
            ExitCritical(basepri);
            return false;
        } else if (wait_res == WAIT_SIGNALED && queue->messageNumber < queue->nodeNumber) {
            cur_tcb->waitResult = WAIT_NONE;
            WriteToQueue(queue, buff, cur_prio);
            ExitCritical(basepri);
            return true;
        }

        cur_tcb->waitResult = WAIT_PENDING;
        cur_tcb->waitTable = &queue->sendTable;

        StateAdd(cur_tcb, cur_tcb->waitTable);
        StateAdd(cur_tcb, &stateTable[BLOCK]);
        StateAdd(cur_tcb, &stateTable[DELAY]);
        wakeTicksTable[cur_prio] = end_tick;
        StateRemove(cur_tcb, &stateTable[READY]);

        SwitchTask();
        ExitCritical(basepri);

        basepri = EnterCritical();

    }
}

uint8_t QueueReceive(Queue_t *queue, void *buff, uint32_t ticks) {
    uint32_t basepri = EnterCritical();
    TCB_t *cur_tcb = GetCurrentTCB();
    uint8_t cur_prio = cur_tcb->priority;

    if (queue->messageNumber > 0) {
        ExtractFromQueue(queue, buff, cur_prio);
        ExitCritical(basepri);
        return true;
    }

    if (ticks == 0) {
        ExitCritical(basepri);
        return false;
    }

    uint32_t end_tick = tickBase + ticks;

    for (;;) {
        WaitResult_t wait_res = cur_tcb->waitResult;

        if (wait_res == WAIT_TIMEOUT || IsTickReached(tickBase, end_tick)) {
            cur_tcb->waitResult = WAIT_NONE;
            ExitCritical(basepri);
            return false;
        } else if (wait_res == WAIT_SIGNALED && queue->messageNumber > 0) {
            cur_tcb->waitResult = WAIT_NONE;
            ExtractFromQueue(queue, buff, cur_prio);
            ExitCritical(basepri);
            return true;
        }

        /* A wake-up does not reserve a message; register again if it was taken. */
        cur_tcb->waitResult = WAIT_PENDING;
        cur_tcb->waitTable = &queue->receiveTable;

        StateAdd(cur_tcb, cur_tcb->waitTable);
        StateAdd(cur_tcb, &stateTable[BLOCK]);
        StateAdd(cur_tcb, &stateTable[DELAY]);
        wakeTicksTable[cur_prio] = end_tick;
        StateRemove(cur_tcb, &stateTable[READY]);

        SwitchTask();
        ExitCritical(basepri);

        basepri = EnterCritical();
    }
}
