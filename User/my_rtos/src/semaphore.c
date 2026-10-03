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