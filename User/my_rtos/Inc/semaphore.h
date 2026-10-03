#pragma once

#include "config.h"

Class (Semaphore_t) {
    uint8_t value;
    uint32_t blockTable;
};

Semaphore_t *SemaphoreCreate(uint8_t value);
uint8_t SemaphoreDelete(Semaphore_t *semaphore);
uint8_t SemaphoreRelease(Semaphore_t *semaphore);
uint8_t SemaphoreTake(Semaphore_t *semaphore, uint32_t ticks);
