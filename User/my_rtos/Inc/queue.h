#pragma once

#include "sparrow.h"

Class (Queue_t) {
    uint8_t *startPoint;
    uint8_t *endPoint;
    uint8_t *readPoint;
    uint8_t *writePoint;
    uint8_t messageNumber;
    uint32_t sendTable;
    uint32_t receiveTable;
    uint32_t nodeSize;
    uint32_t nodeNumber;
};

Queue_t *QueueCrate(uint32_t queueLength, uint32_t queueSize);

void QueueDelete(Queue_t *queue);

void WriteToQueue(Queue_t *queue, void *buff, uint32_t curreentTcbPriority);