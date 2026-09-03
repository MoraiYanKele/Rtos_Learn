#include "queue.h"

Queue_t *QueueCrate(uint32_t queueLength, uint32_t queueSize) {
    size_t q_size = (size_t)(queueLength * queueSize);
    Queue_t *queue = Heap_Malloc(sizeof(Queue_t) + q_size);
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

void WriteToQueue(Queue_t *queue, void *buff, uint32_t curreentTcbPriority) {
    memcpy((void *)queue->writePoint, buff, (size_t)queue->nodeSize);
    queue->messageNumber++;

    queue->writePoint += queue->nodeSize;
    if (queue->writePoint >= queue->endPoint) {
        queue->writePoint = queue->startPoint;
    }

    if (queue->receiveTable != 0) {
        uint8_t priority = FindTopTcbIndex(curreentTcbPriority);
        TaskHandle_t task = GetTaskHandle(priority);

        StateRemove(task, queue->receiveTable);
        StateRemove(task, )
    }




}



