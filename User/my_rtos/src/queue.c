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

void QueueDelete(Queue_t *queue) {
    Heap_Free(queue);
}

#define FindTopTcbIndex FindHighestPriority

// 写入队列
void WriteToQueue(Queue_t *queue, void *buff, uint32_t currentTcbPriority) {
    memcpy((void *)queue->writePoint, buff, (size_t)queue->nodeSize);

    queue->writePoint += queue->nodeSize;
    if (queue->writePoint >= queue->endPoint) {
        queue->writePoint = queue->startPoint;
    }

    if (queue->receiveTable != 0) {
        uint8_t priority = FindTopTcbIndex(queue->receiveTable);
        TaskHandle_t task = GetTaskHandle(priority);

        StateRemove(task, &queue->receiveTable);
        StateRemove(task, &stateTable[BLOCK]);
        StateRemove(task, &stateTable[DELAY]);
        StateAdd(task, &stateTable[READY]);
        if (priority > currentTcbPriority) {
            SwitchTask();
        }

        queue->messageNumber++;
    }
}

// 读取队列
void ExtractFromQueue(Queue_t *queue, void *buff, uint32_t currentTcbPriority) {
    queue->readPoint += queue->nodeSize;
    if (queue->readPoint >= queue->endPoint) {
        queue->readPoint = queue->startPoint;
    }

    memcpy(buff, (void *)queue->readPoint, (size_t)queue->nodeSize);

    if (queue->sendTable != 0) {
        uint8_t priority = FindTopTcbIndex(queue->sendTable);
        TaskHandle_t task = GetTaskHandle(priority);

        StateRemove(task, &queue->sendTable);
        StateRemove(task, &stateTable[DELAY]);
        StateRemove(task, &stateTable[BLOCK]);
        StateAdd(task, &stateTable[READY]);
        if (priority > currentTcbPriority) {
            SwitchTask();
        }
    }

    queue->messageNumber--;
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

    StateAdd(cur_tcb, &queue->sendTable);
    StateAdd(cur_tcb, &stateTable[BLOCK]);
    wakeTicksTable[cur_prio] = tickBase + ticks;
    StateAdd(cur_tcb, &stateTable[DELAY]);
    StateRemove(cur_tcb, &stateTable[READY]);

    SwitchTask();
    ExitCritical(basepri);

    basepri = EnterCritical();
    if (CheckState(cur_tcb, &stateTable[BLOCK])) {
        StateRemove(cur_tcb, &queue->sendTable);
        StateRemove(cur_tcb, &stateTable[BLOCK]);
        StateRemove(cur_tcb, &stateTable[DELAY]);
        ExitCritical(basepri);
        return false;        
    } else {
        WriteToQueue(queue, buff, cur_prio);
        ExitCritical(basepri);
        return true;
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

    StateAdd(cur_tcb, &queue->receiveTable);
    StateAdd(cur_tcb, &stateTable[BLOCK]);
    wakeTicksTable[cur_prio] = tickBase + ticks;
    StateAdd(cur_tcb, &stateTable[DELAY]);
    StateRemove(cur_tcb, &stateTable[READY]);

    SwitchTask();
    ExitCritical(basepri);

    basepri = EnterCritical();
    if (CheckState(cur_tcb, &stateTable[BLOCK])) {
        StateRemove(cur_tcb, &queue->receiveTable);
        StateRemove(cur_tcb, &stateTable[BLOCK]);
        StateRemove(cur_tcb, &stateTable[DELAY]);
        ExitCritical(basepri);
        return false;        
    } else {
        ExtractFromQueue(queue, buff, cur_prio);
        ExitCritical(basepri);
        return true;
    }
}