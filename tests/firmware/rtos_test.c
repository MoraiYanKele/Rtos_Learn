#include "rtos_test.h"
#include "schedule.h"
#include "semaphore.h"
#include "queue.h"
#include "usart.h"
#include <stdio.h>
#include <string.h>

/* Workers never print: UART traffic must not determine the scheduling order. */
#define WAIT_TICKS 20U
#define JOIN_TICKS 500U
#define HIGH_PRIORITY 31U
#define MID_PRIORITY 8U
#define TRACE_SIZE 32U
#define STRESS_COUNT 128U

typedef struct { uint32_t sequence; uint32_t check; } Message;
typedef struct { uint8_t ok; uint32_t actual; uint32_t expected; } Result;
typedef enum {
    JOB_DELAY, JOB_SEMAPHORE, JOB_RECEIVE, JOB_SEND,
    JOB_REFILL, JOB_STEAL, JOB_SEM_STRESS, JOB_QUEUE_STRESS
} Job;
typedef struct {
    Semaphore_t *gate;
    Semaphore_t *done;
    TaskHandle_t task;
    uint32_t priority;
    volatile Job job;
    Queue_t *queue;
    Message message;
    volatile uint32_t success;
    volatile uint32_t elapsed;
    volatile uint32_t completed;
    volatile uint32_t count;
} Worker;

static Worker highWorker, midWorker;
static TaskHandle_t controllerTask;
static Semaphore_t *testSemaphore;
static Queue_t *testQueue, *sendRaceQueue, *receiveRaceQueue;
static volatile uint32_t trace[TRACE_SIZE], traceLength;
static volatile uint32_t lowerRan, payload;
static uint8_t abortSuite;
static char logBuffer[160];

static Result MakeResult(uint8_t ok, uint32_t actual, uint32_t expected) {
    return (Result){ok, actual, expected};
}
static uint32_t Now(void) {
    uint32_t mask = EnterCritical();
    uint32_t now = tickBase;
    ExitCritical(mask);
    return now;
}
static void Trace(uint32_t event) {
    uint32_t mask = EnterCritical();
    if (traceLength < TRACE_SIZE) trace[traceLength++] = event;
    ExitCritical(mask);
}
static void ResetTrace(void) {
    uint32_t mask = EnterCritical();
    traceLength = 0;
    ExitCritical(mask);
}
static uint8_t TraceEquals(uint32_t a, uint32_t b, uint32_t c, uint32_t length) {
    return traceLength == length && trace[0] == a &&
        (length < 2 || trace[1] == b) && (length < 3 || trace[2] == c);
}
static uint8_t Timed(uint32_t elapsed) {
    /* One tick of quantisation and five ticks of scheduling tolerance. */
    return elapsed >= WAIT_TICKS && elapsed <= WAIT_TICKS + 5U;
}
static uint8_t CleanWait(TaskHandle_t task, uint32_t bitmap) {
    uint32_t bit = 1UL << task->priority;
    return task->waitTable == NULL && task->waitResult == WAIT_NONE &&
        (stateTable[BLOCK] & bit) == 0 && (stateTable[DELAY] & bit) == 0 &&
        (stateTable[READY] & bit) != 0 && (bitmap & bit) == 0;
}
static Message MakeMessage(uint32_t sequence) {
    return (Message){sequence, sequence ^ 0xA55AA55AUL};
}
static uint8_t IsMessage(Message message, uint32_t sequence) {
    return message.sequence == sequence && message.check == (sequence ^ 0xA55AA55AUL);
}
static void Log(const char *line) {
    if (HAL_UART_Transmit(&huart1, (uint8_t *)line, (uint16_t)strlen(line), 100) != HAL_OK) {
        /* A truncated UART record must never be turned into a passing suite. */
        Error_Handler();
    }
}
static void Start(Worker *worker, Job job, Queue_t *queue, uint32_t sequence) {
    worker->job = job;
    worker->queue = queue;
    worker->message = MakeMessage(sequence);
    worker->success = worker->elapsed = worker->completed = worker->count = 0;
    __DMB();
    if (!SemaphoreRelease(worker->gate)) abortSuite = 1;
}
static uint8_t Join(Worker *worker) {
    if (!SemaphoreTake(worker->done, JOIN_TICKS)) {
        abortSuite = 1;
        return 0;
    }
    return 1;
}
static void WorkerTask(void *parameters) {
    Worker *worker = parameters;
    Trace(worker->priority);
    for (;;) {
        if (!SemaphoreTake(worker->gate, 60000U)) continue;
        uint32_t start = Now();
        switch (worker->job) {
        case JOB_DELAY:
            TaskDelay(WAIT_TICKS);
            worker->success = lowerRan != 0;
            break;
        case JOB_SEMAPHORE:
            worker->success = SemaphoreTake(testSemaphore, WAIT_TICKS * 4U);
            Trace(worker->priority);
            break;
        case JOB_RECEIVE:
            worker->message = MakeMessage(0xDEADU);
            worker->success = QueueReceive(worker->queue, &worker->message, WAIT_TICKS * 4U);
            Trace(worker->priority);
            break;
        case JOB_SEND:
            worker->success = QueueSend(worker->queue, &worker->message, WAIT_TICKS * 4U);
            Trace(worker->priority);
            break;
        case JOB_REFILL: {
            Message message;
            uint8_t read = QueueReceive(worker->queue, &message, 0);
            Message replacement = MakeMessage(99);
            worker->success = read && IsMessage(message, 1) &&
                QueueSend(worker->queue, &replacement, 0);
            break;
        }
        case JOB_STEAL: {
            Message message = MakeMessage(99);
            uint8_t sent = QueueSend(worker->queue, &message, 0);
            worker->success = sent && QueueReceive(worker->queue, &message, 0) &&
                IsMessage(message, 99);
            break;
        }
        case JOB_SEM_STRESS:
            worker->success = 1;
            for (uint32_t i = 1; i <= STRESS_COUNT; ++i) {
                if (!SemaphoreTake(testSemaphore, JOIN_TICKS) || payload != i) {
                    worker->success = 0;
                    break;
                }
                worker->count++;
            }
            break;
        case JOB_QUEUE_STRESS:
            worker->success = 1;
            for (uint32_t i = 1; i <= STRESS_COUNT; ++i) {
                Message message;
                if (!QueueReceive(testQueue, &message, JOIN_TICKS) || !IsMessage(message, i)) {
                    worker->success = 0;
                    break;
                }
                worker->count++;
            }
            break;
        }
        worker->elapsed = Now() - start;
        worker->completed = 1;
        SemaphoreRelease(worker->done);
    }
}

static Result TestBoot(void) {
    return MakeResult(TraceEquals(HIGH_PRIORITY, MID_PRIORITY, 1, 3), traceLength, 3);
}
static Result TestDelay(void) {
    lowerRan = 0;
    Start(&highWorker, JOB_DELAY, NULL, 0);
    lowerRan = 1;
    uint8_t joined = Join(&highWorker);
    return MakeResult(joined && highWorker.success && Timed(highWorker.elapsed), highWorker.elapsed, WAIT_TICKS);
}
static Result TestPreemption(void) {
    lowerRan = 0;
    Start(&highWorker, JOB_DELAY, NULL, 0);
    lowerRan = 1;
    uint32_t start = HAL_GetTick();
    /* Deliberately no RTOS call or yield: only SysTick can preempt this loop. */
    while (!highWorker.completed && (uint32_t)(HAL_GetTick() - start) < 200U) __NOP();
    uint8_t preempted = highWorker.completed != 0;
    uint8_t joined = Join(&highWorker);
    return MakeResult(preempted && joined && Timed(highWorker.elapsed), highWorker.elapsed, WAIT_TICKS);
}
static Result TestZeroDelay(void) {
    uint32_t start = Now();
    TaskDelay(0);
    uint32_t elapsed = Now() - start;
    return MakeResult(elapsed <= 1U && CleanWait(controllerTask, 0), elapsed, 0);
}
static Result TestCritical(void) {
    uint32_t before = __get_BASEPRI();
    uint32_t outer = EnterCritical();
    uint32_t masked = __get_BASEPRI();
    uint32_t inner = EnterCritical();
    ExitCritical(inner);
    uint32_t nested = __get_BASEPRI();
    ExitCritical(outer);
    uint32_t after = __get_BASEPRI();
    return MakeResult(before == after && masked == CONFIG_SHIELD_INTER_PRIORITY && nested == masked,
                      nested, CONFIG_SHIELD_INTER_PRIORITY);
}
static Result TestSemCount(void) {
    uint8_t ok = SemaphoreRelease(testSemaphore) && SemaphoreRelease(testSemaphore);
    ok &= SemaphoreTake(testSemaphore, 0) && SemaphoreTake(testSemaphore, 0);
    ok &= !SemaphoreTake(testSemaphore, 0) && testSemaphore->value == 0;
    return MakeResult(ok, testSemaphore->value, 0);
}
static Result TestSemPoll(void) {
    uint32_t start = Now();
    uint8_t taken = SemaphoreTake(testSemaphore, 0);
    uint32_t elapsed = Now() - start;
    return MakeResult(!taken && elapsed <= 1U && CleanWait(controllerTask, testSemaphore->blockTable), elapsed, 0);
}
static Result TestSemTimeout(void) {
    uint32_t start = Now();
    uint8_t taken = SemaphoreTake(testSemaphore, WAIT_TICKS);
    uint32_t elapsed = Now() - start;
    return MakeResult(!taken && Timed(elapsed) && CleanWait(controllerTask, testSemaphore->blockTable), elapsed, WAIT_TICKS);
}
static Result TestSemWake(void) {
    ResetTrace();
    Start(&highWorker, JOB_SEMAPHORE, NULL, 0);
    uint8_t blocked = (testSemaphore->blockTable & (1UL << HIGH_PRIORITY)) != 0;
    Trace(1);
    uint8_t released = SemaphoreRelease(testSemaphore);
    Trace(2);
    uint8_t joined = Join(&highWorker);
    return MakeResult(blocked && released && joined && highWorker.success &&
                      TraceEquals(1, HIGH_PRIORITY, 2, 3), traceLength, 3);
}
static Result TestSemPriority(void) {
    ResetTrace();
    Start(&highWorker, JOB_SEMAPHORE, NULL, 0);
    Start(&midWorker, JOB_SEMAPHORE, NULL, 0);
    uint32_t expected = (1UL << HIGH_PRIORITY) | (1UL << MID_PRIORITY);
    uint8_t blocked = testSemaphore->blockTable == expected;
    SemaphoreRelease(testSemaphore);
    SemaphoreRelease(testSemaphore);
    uint8_t highJoined = Join(&highWorker), midJoined = Join(&midWorker);
    return MakeResult(blocked && highJoined && midJoined && highWorker.success && midWorker.success &&
                      TraceEquals(HIGH_PRIORITY, MID_PRIORITY, 0, 2), traceLength, 2);
}
static Result TestSemDelete(void) {
    Start(&highWorker, JOB_SEMAPHORE, NULL, 0);
    uint8_t rejected = !SemaphoreDelete(testSemaphore);
    if (!rejected) { abortSuite = 1; return MakeResult(0, 1, 0); }
    SemaphoreRelease(testSemaphore);
    uint8_t joined = Join(&highWorker);
    return MakeResult(joined && highWorker.success && testSemaphore->blockTable == 0, rejected, 1);
}
static Result TestSemCleanup(void) {
    uint8_t timedOut = !SemaphoreTake(testSemaphore, WAIT_TICKS);
    uint8_t clean = CleanWait(controllerTask, testSemaphore->blockTable);
    SemaphoreRelease(testSemaphore);
    uint8_t taken = SemaphoreTake(testSemaphore, 0);
    return MakeResult(timedOut && clean && taken && testSemaphore->value == 0, testSemaphore->blockTable, 0);
}
static Result TestQueueFifo(void) {
    uint8_t ok = 1;
    for (uint32_t round = 0; round < 16; ++round) {
        for (uint32_t i = 1; i <= 3; ++i) {
            Message message = MakeMessage(round * 3 + i);
            ok &= QueueSend(testQueue, &message, 0);
            /* Changing the caller's buffer must not change the queued copy. */
            message.sequence = message.check = 0;
        }
        for (uint32_t i = 1; i <= 3; ++i) {
            Message message = {0, 0};
            ok &= QueueReceive(testQueue, &message, 0) && IsMessage(message, round * 3 + i);
        }
    }
    return MakeResult(ok && testQueue->messageNumber == 0, testQueue->messageNumber, 0);
}
static uint8_t FillQueue(void) {
    uint8_t ok = 1;
    for (uint32_t i = 1; i <= 3; ++i) {
        Message message = MakeMessage(i);
        ok &= QueueSend(testQueue, &message, 0);
    }
    return ok;
}
static uint8_t DrainQueue(uint32_t first, uint32_t last) {
    uint8_t ok = 1;
    for (uint32_t i = first; i <= last; ++i) {
        Message message = {0, 0};
        ok &= QueueReceive(testQueue, &message, 0) && IsMessage(message, i);
    }
    return ok;
}
static Result TestQueuePoll(void) {
    Message message = MakeMessage(99);
    uint8_t empty = !QueueReceive(testQueue, &message, 0) && IsMessage(message, 99);
    uint8_t filled = FillQueue();
    uint32_t start = Now();
    uint8_t full = !QueueSend(testQueue, &message, 0);
    uint32_t elapsed = Now() - start;
    uint8_t drained = DrainQueue(1, 3);
    return MakeResult(empty && filled && full && drained && elapsed <= 1U && testQueue->messageNumber == 0,
                      elapsed, 0);
}
static Result TestQueueSendTimeout(void) {
    uint8_t filled = FillQueue();
    Message message = MakeMessage(4);
    uint32_t start = Now();
    uint8_t sent = QueueSend(testQueue, &message, WAIT_TICKS);
    uint32_t elapsed = Now() - start;
    uint8_t clean = CleanWait(controllerTask, testQueue->sendTable) && testQueue->messageNumber == 3;
    uint8_t drained = DrainQueue(1, 3);
    return MakeResult(filled && !sent && Timed(elapsed) && clean && drained, elapsed, WAIT_TICKS);
}
static Result TestQueueReceiveTimeout(void) {
    Message message = MakeMessage(99);
    uint32_t start = Now();
    uint8_t received = QueueReceive(testQueue, &message, WAIT_TICKS);
    uint32_t elapsed = Now() - start;
    return MakeResult(!received && Timed(elapsed) && IsMessage(message, 99) &&
                      CleanWait(controllerTask, testQueue->receiveTable) && testQueue->messageNumber == 0,
                      elapsed, WAIT_TICKS);
}
static Result TestQueueReceiveWake(void) {
    ResetTrace();
    Start(&highWorker, JOB_RECEIVE, testQueue, 0);
    uint8_t blocked = testQueue->receiveTable == (1UL << HIGH_PRIORITY);
    Message message = MakeMessage(42);
    uint8_t sent = QueueSend(testQueue, &message, 0);
    uint8_t immediate = highWorker.completed != 0;
    uint8_t joined = Join(&highWorker);
    return MakeResult(blocked && sent && immediate && joined && highWorker.success &&
                      IsMessage(highWorker.message, 42) && testQueue->messageNumber == 0,
                      highWorker.message.sequence, 42);
}
static Result TestQueueSendWake(void) {
    uint8_t filled = FillQueue();
    Start(&highWorker, JOB_SEND, testQueue, 4);
    uint8_t blocked = testQueue->sendTable == (1UL << HIGH_PRIORITY);
    Message message;
    uint8_t read = QueueReceive(testQueue, &message, 0) && IsMessage(message, 1);
    uint8_t immediate = highWorker.completed != 0;
    uint8_t joined = Join(&highWorker);
    uint8_t drained = DrainQueue(2, 4);
    return MakeResult(filled && blocked && read && immediate && joined && highWorker.success && drained,
                      testQueue->messageNumber, 0);
}
static Result TestQueueDelete(void) {
    Start(&highWorker, JOB_RECEIVE, testQueue, 0);
    uint8_t rejected = !QueueDelete(testQueue);
    if (!rejected) { abortSuite = 1; return MakeResult(0, 1, 0); }
    Message message = MakeMessage(42);
    QueueSend(testQueue, &message, 0);
    uint8_t joined = Join(&highWorker);
    return MakeResult(joined && highWorker.success && IsMessage(highWorker.message, 42), rejected, 1);
}
static Result TestSemStress(void) {
    Start(&highWorker, JOB_SEM_STRESS, NULL, 0);
    uint8_t ok = 1;
    for (uint32_t i = 1; i <= STRESS_COUNT; ++i) {
        payload = i;
        __DMB();
        ok &= SemaphoreRelease(testSemaphore);
    }
    uint8_t joined = Join(&highWorker);
    return MakeResult(ok && joined && highWorker.success && highWorker.count == STRESS_COUNT &&
                      testSemaphore->value == 0 && testSemaphore->blockTable == 0,
                      highWorker.count, STRESS_COUNT);
}
static Result TestQueueStress(void) {
    Start(&highWorker, JOB_QUEUE_STRESS, testQueue, 0);
    uint8_t ok = 1;
    for (uint32_t i = 1; i <= STRESS_COUNT; ++i) {
        Message message = MakeMessage(i);
        if (!QueueSend(testQueue, &message, JOIN_TICKS)) { ok = 0; break; }
    }
    uint8_t joined = Join(&highWorker);
    return MakeResult(ok && joined && highWorker.success && highWorker.count == STRESS_COUNT &&
                      testQueue->messageNumber == 0 && testQueue->receiveTable == 0 && testQueue->sendTable == 0,
                      highWorker.count, STRESS_COUNT);
}
static uint32_t SetTick(uint32_t value) {
    uint32_t mask = EnterCritical();
    uint32_t old = tickBase;
    tickBase = value;
    ExitCritical(mask);
    return old;
}
static Result TestDelayWrap(void) {
    uint32_t old = SetTick(UINT32_MAX - 10U);
    Result result = TestDelay();
    SetTick(old + highWorker.elapsed);
    return result;
}
static Result TestSemWrap(void) {
    uint32_t old = SetTick(UINT32_MAX - 10U);
    Result result = TestSemTimeout();
    SetTick(old + result.actual);
    return result;
}
static Result TestSendContention(void) {
    Message initial = MakeMessage(1);
    uint8_t filled = QueueSend(sendRaceQueue, &initial, 0);
    Start(&midWorker, JOB_SEND, sendRaceQueue, 2);
    uint8_t blocked = sendRaceQueue->sendTable == (1UL << MID_PRIORITY);
    /* High receiver frees a slot, then takes it back before the sender can run. */
    Start(&highWorker, JOB_REFILL, sendRaceQueue, 0);
    uint8_t highJoined = Join(&highWorker), midJoined = Join(&midWorker);
    uint32_t count = sendRaceQueue->messageNumber;
    Message remaining = {0, 0};
    uint8_t read = QueueReceive(sendRaceQueue, &remaining, 0);
    return MakeResult(filled && blocked && highJoined && midJoined && highWorker.success &&
                      !midWorker.success && midWorker.elapsed >= WAIT_TICKS * 4U && count == 1 &&
                      read && IsMessage(remaining, 99) && sendRaceQueue->sendTable == 0,
                      count, 1);
}
static Result TestReceiveContention(void) {
    Start(&midWorker, JOB_RECEIVE, receiveRaceQueue, 0);
    uint8_t blocked = receiveRaceQueue->receiveTable == (1UL << MID_PRIORITY);
    /* High sender sends, then consumes its own message before the receiver runs. */
    Start(&highWorker, JOB_STEAL, receiveRaceQueue, 0);
    uint8_t highJoined = Join(&highWorker), midJoined = Join(&midWorker);
    return MakeResult(blocked && highJoined && midJoined && highWorker.success && !midWorker.success &&
                      midWorker.elapsed >= WAIT_TICKS * 4U && IsMessage(midWorker.message, 0xDEADU) &&
                      receiveRaceQueue->messageNumber == 0 && receiveRaceQueue->receiveTable == 0,
                      receiveRaceQueue->messageNumber, 0);
}

typedef struct { const char *id; Result (*run)(void); } TestCase;
#define RTOS_CASE(id, function) {#id, function},
static const TestCase cases[] = {
#include "rtos_test_cases.def"
};
#undef RTOS_CASE
#define CASE_COUNT (sizeof(cases) / sizeof(cases[0]))

static void WaitForRun(void) {
    char command[16];
    uint32_t length = 0, lastReady = HAL_GetTick() - 1000U;
    uint8_t overflow = 0;
    for (;;) {
        if ((uint32_t)(HAL_GetTick() - lastReady) >= 1000U) {
            snprintf(logBuffer, sizeof(logBuffer), "RTOS_TEST READY version=1 total=%lu\r\n", (unsigned long)CASE_COUNT);
            Log(logBuffer);
            lastReady = HAL_GetTick();
        }
        uint8_t ch;
        if (HAL_UART_Receive(&huart1, &ch, 1, 10) == HAL_OK) {
            if (ch == '\n' || ch == '\r') {
                command[length] = '\0';
                if (!overflow && strcmp(command, "RUN") == 0) return;
                length = 0;
                overflow = 0;
            } else if (length < sizeof(command) - 1) command[length++] = (char)ch;
            else overflow = 1;
        } else {
            TaskDelay(1);
        }
    }
}
static void Controller(void *parameters) {
    (void)parameters;
    Trace(1);
    WaitForRun();
    snprintf(logBuffer, sizeof(logBuffer), "RTOS_TEST START version=1 total=%lu\r\n", (unsigned long)CASE_COUNT);
    Log(logBuffer);
    uint32_t passed = 0, failed = 0;
    for (uint32_t i = 0; i < CASE_COUNT; ++i) {
        snprintf(logBuffer, sizeof(logBuffer), "RTOS_TEST BEGIN id=%s\r\n", cases[i].id);
        Log(logBuffer);
        Result result = cases[i].run();
        if (result.ok) passed++; else failed++;
        snprintf(logBuffer, sizeof(logBuffer), "RTOS_TEST CASE id=%s status=%s actual=%lu expected=%lu\r\n",
                 cases[i].id, result.ok ? "PASS" : "FAIL", (unsigned long)result.actual, (unsigned long)result.expected);
        Log(logBuffer);
        if (abortSuite) {
            Log("RTOS_TEST ABORT reason=worker_or_object_failure\r\n");
            break;
        }
    }
    snprintf(logBuffer, sizeof(logBuffer), "RTOS_TEST SUMMARY status=%s passed=%lu failed=%lu total=%lu\r\n",
             failed == 0 && !abortSuite ? "PASS" : "FAIL", (unsigned long)passed, (unsigned long)failed, (unsigned long)CASE_COUNT);
    Log(logBuffer);
    /* One suite per reset; never silently reuse possibly corrupted IPC objects. */
    for (;;) TaskDelay(1000);
}
void RtosTestCreateTasks(void) {
    testSemaphore = SemaphoreCreate(0);
    highWorker.gate = SemaphoreCreate(0);
    highWorker.done = SemaphoreCreate(0);
    midWorker.gate = SemaphoreCreate(0);
    midWorker.done = SemaphoreCreate(0);
    testQueue = QueueCrate(3, sizeof(Message));
    sendRaceQueue = QueueCrate(1, sizeof(Message));
    receiveRaceQueue = QueueCrate(1, sizeof(Message));
    if (!testSemaphore || !highWorker.gate || !highWorker.done || !midWorker.gate ||
        !midWorker.done || !testQueue || !sendRaceQueue || !receiveRaceQueue) {
        Log("RTOS_TEST ABORT reason=allocation_failure\r\n");
        Error_Handler();
    }
    highWorker.priority = HIGH_PRIORITY;
    midWorker.priority = MID_PRIORITY;
    /* All allocations occur before scheduling; total is below the 8 KiB heap. */
    TaskCreate(Controller, 512, NULL, 1, &controllerTask);
    TaskCreate(WorkerTask, 256, &midWorker, MID_PRIORITY, &midWorker.task);
    TaskCreate(WorkerTask, 256, &highWorker, HIGH_PRIORITY, &highWorker.task);
}
