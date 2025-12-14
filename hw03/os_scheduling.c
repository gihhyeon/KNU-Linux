#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

enum { CHILD_PROCESS_COUNT = 10 };

typedef enum {
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_SLEEPING,
    PROCESS_DONE
} ProcessState;

typedef struct {
    pid_t        processId;
    ProcessState state;
    int          remainingTimeQuantum;
    int          wakeUpTick;                    // sleeping -> ready로 돌아올 tick

    // readyQueue 체류 시간 측정을 위한 필드
    int          isInReadyQueue;                // readyQueue에 실제로 들어가 있는지 여부(중복 enqueue 방지)
    long         readyQueueEnterTick;           // readyQueue에 "이번에" 들어간 시점(tick)
    long         totalReadyQueueWaitingTicks;   // readyQueue에서 기다린 시간 누적
} ProcessControlBlock;

// random utils
static int getRandomInclusive(int minValue, int maxValue) {
    return minValue + (rand() % (maxValue - minValue + 1));
}

//FIFO queue (ready / zeroQuantum)
typedef struct {
    int buffer[256];   // PCB index 저장
    int head;
    int tail;
    int size;
} IndexQueue;

static void initializeQueue(IndexQueue *queue) {
    queue->head = 0;
    queue->tail = 0;
    queue->size = 0;
}

static int isQueueEmpty(const IndexQueue *queue) {
    return queue->size == 0;
}

static void enqueueIndex(IndexQueue *queue, int value) {
    queue->buffer[queue->tail] = value;
    queue->tail = (queue->tail + 1) % (int)(sizeof(queue->buffer) / sizeof(queue->buffer[0]));
    queue->size++;
}

static int dequeueIndex(IndexQueue *queue) {
    int value = queue->buffer[queue->head];
    queue->head = (queue->head + 1) % (int)(sizeof(queue->buffer) / sizeof(queue->buffer[0]));
    queue->size--;
    return value;
}

// Min-Heap (sleepingQueue: NOT FIFO)
typedef struct {
    int heapIndices[256];  // PCB index 저장
    int heapSize;
    ProcessControlBlock *processTable;
} WakeUpMinHeap;

static void initializeWakeUpHeap(WakeUpMinHeap *heap, ProcessControlBlock *processTable) {
    heap->heapSize = 0;
    heap->processTable = processTable;
}

static int isWakeUpHeapEmpty(const WakeUpMinHeap *heap) {
    return heap->heapSize == 0;
}

static int hasHigherPriority(const WakeUpMinHeap *heap, int pcbIndexA, int pcbIndexB) {
    int wakeA = heap->processTable[pcbIndexA].wakeUpTick;
    int wakeB = heap->processTable[pcbIndexB].wakeUpTick;
    if (wakeA != wakeB) return (wakeA < wakeB);
    return (pcbIndexA < pcbIndexB);
}

static void swapInt(int *a, int *b) {
    int temp = *a;
    *a = *b;
    *b = temp;
}

static void pushWakeUpHeap(WakeUpMinHeap *heap, int pcbIndex) {
    int currentIndex = heap->heapSize++;
    heap->heapIndices[currentIndex] = pcbIndex;

    while (currentIndex > 0) {
        int parentIndex = (currentIndex - 1) / 2;
        if (hasHigherPriority(heap, heap->heapIndices[parentIndex], heap->heapIndices[currentIndex])) break;
        swapInt(&heap->heapIndices[parentIndex], &heap->heapIndices[currentIndex]);
        currentIndex = parentIndex;
    }
}

static int peekWakeUpHeapTop(const WakeUpMinHeap *heap) {
    return heap->heapIndices[0];
}

static int popWakeUpHeap(WakeUpMinHeap *heap) {
    int topIndex = heap->heapIndices[0];
    heap->heapIndices[0] = heap->heapIndices[--heap->heapSize];

    int currentIndex = 0;
    while (1) {
        int leftChild = 2 * currentIndex + 1;
        int rightChild = 2 * currentIndex + 2;
        int smallest = currentIndex;

        if (leftChild < heap->heapSize &&
            !hasHigherPriority(heap, heap->heapIndices[smallest], heap->heapIndices[leftChild])) {
            smallest = leftChild;
        }
        if (rightChild < heap->heapSize &&
            !hasHigherPriority(heap, heap->heapIndices[smallest], heap->heapIndices[rightChild])) {
            smallest = rightChild;
        }
        if (smallest == currentIndex) break;

        swapInt(&heap->heapIndices[currentIndex], &heap->heapIndices[smallest]);
        currentIndex = smallest;
    }
    return topIndex;
}

// parent signal globals
static volatile sig_atomic_t shouldReapExitedChildren = 0;
static volatile sig_atomic_t ioRequestedProcessId = 0;
static volatile sig_atomic_t stopScheduler = 0;

static void handleSigChild(int signalNumber) {
    (void)signalNumber;
    shouldReapExitedChildren = 1;
}

static void handleIoRequest(int signalNumber, siginfo_t *signalInfo, void *context) {
    (void)signalNumber;
    (void)context;
    ioRequestedProcessId = (sig_atomic_t)signalInfo->si_pid;
}

static void handleSigInt(int signalNumber) {
    (void)signalNumber;
    stopScheduler = 1;
}

//child behavior
static pid_t parentProcessId = -1;
static int remainingCpuBurst = 0;

static void handleRunOneTick(int signalNumber) {
    (void)signalNumber;

    if (remainingCpuBurst > 0) remainingCpuBurst--;

    if (remainingCpuBurst == 0) {
        int chooseExitOrIo = getRandomInclusive(0, 1); // 0: exit, 1: io
        if (chooseExitOrIo == 0) {
            _exit(0);
        } else {
            kill(parentProcessId, SIGUSR2);
            remainingCpuBurst = getRandomInclusive(1, 10);
        }
    }
}

//helpers
static int findPcbIndexByPid(ProcessControlBlock processTable[], pid_t pid) {
    for (int i = 0; i < CHILD_PROCESS_COUNT; i++) {
        if (processTable[i].processId == pid) return i;
    }
    return -1;
}

static int isAnyProcessAlive(ProcessControlBlock processTable[]) {
    for (int i = 0; i < CHILD_PROCESS_COUNT; i++) {
        if (processTable[i].state != PROCESS_DONE) return 1;
    }
    return 0;
}

static int areAllTimeQuantumsZero(ProcessControlBlock processTable[]) {
    for (int i = 0; i < CHILD_PROCESS_COUNT; i++) {
        if (processTable[i].state != PROCESS_DONE && processTable[i].remainingTimeQuantum > 0) return 0;
    }
    return 1;
}

// readyQueue measurement helpers
static void enqueueToReadyQueue(IndexQueue *readyQueue,
                                ProcessControlBlock processTable[],
                                int pcbIndex,
                                long currentTick) {
    // 중복 enqueue 방지
    if (processTable[pcbIndex].isInReadyQueue) return;

    processTable[pcbIndex].isInReadyQueue = 1;
    processTable[pcbIndex].readyQueueEnterTick = currentTick;

    enqueueIndex(readyQueue, pcbIndex);
}

static void onDequeuedFromReadyQueue(ProcessControlBlock processTable[],
                                     int pcbIndex,
                                     long currentTick) {
    // readyQueue에서 나오는 순간, 체류 시간을 누적
    if (!processTable[pcbIndex].isInReadyQueue) return;

    long waitedTicks = currentTick - processTable[pcbIndex].readyQueueEnterTick;
    if (waitedTicks < 0) waitedTicks = 0;

    processTable[pcbIndex].totalReadyQueueWaitingTicks += waitedTicks;
    processTable[pcbIndex].isInReadyQueue = 0;
}

// zeroQuantumQueue -> readyQueue
static void moveZeroQuantumQueueToReadyQueue(IndexQueue *zeroQuantumQueue,
                                             IndexQueue *readyQueue,
                                             ProcessControlBlock processTable[],
                                             long currentTick) {
    while (!isQueueEmpty(zeroQuantumQueue)) {
        int pcbIndex = dequeueIndex(zeroQuantumQueue);
        if (processTable[pcbIndex].state == PROCESS_READY &&
            processTable[pcbIndex].remainingTimeQuantum > 0) {
            enqueueToReadyQueue(readyQueue, processTable, pcbIndex, currentTick);
        }
    }
}

// main
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <TIME_QUANTUM> [TICK_USEC=100000] [SEED]\n", argv[0]);
        return 1;
    }

    int initialTimeQuantum = atoi(argv[1]);
    if (initialTimeQuantum <= 0) {
        fprintf(stderr, "TIME_QUANTUM must be > 0\n");
        return 1;
    }

    int tickIntervalUsec = (argc >= 3) ? atoi(argv[2]) : 100000;
    if (tickIntervalUsec <= 0) tickIntervalUsec = 100000;

    unsigned seed = (argc >= 4) ? (unsigned)strtoul(argv[3], NULL, 10)
                                : (unsigned)(time(NULL) ^ getpid());
    srand(seed);

    ProcessControlBlock processTable[CHILD_PROCESS_COUNT];
    memset(processTable, 0, sizeof(processTable));

    IndexQueue readyQueue;
    IndexQueue zeroQuantumQueue;
    initializeQueue(&readyQueue);
    initializeQueue(&zeroQuantumQueue);

    WakeUpMinHeap sleepingQueue;
    initializeWakeUpHeap(&sleepingQueue, processTable);

    /* 부모 시그널 설정 */
    struct sigaction childExitAction;
    memset(&childExitAction, 0, sizeof(childExitAction));
    childExitAction.sa_handler = handleSigChild;
    sigemptyset(&childExitAction.sa_mask);
    childExitAction.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &childExitAction, NULL);

    struct sigaction ioRequestAction;
    memset(&ioRequestAction, 0, sizeof(ioRequestAction));
    ioRequestAction.sa_sigaction = handleIoRequest;
    sigemptyset(&ioRequestAction.sa_mask);
    ioRequestAction.sa_flags = SA_SIGINFO | SA_RESTART;
    sigaction(SIGUSR2, &ioRequestAction, NULL);

    struct sigaction interruptAction;
    memset(&interruptAction, 0, sizeof(interruptAction));
    interruptAction.sa_handler = handleSigInt;
    sigemptyset(&interruptAction.sa_mask);
    interruptAction.sa_flags = SA_RESTART;
    sigaction(SIGINT, &interruptAction, NULL);

    /* 자식 생성 */
    for (int i = 0; i < CHILD_PROCESS_COUNT; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return 1;
        }

        if (pid == 0) {
            parentProcessId = getppid();
            srand((unsigned)(time(NULL) ^ getpid()));
            remainingCpuBurst = getRandomInclusive(1, 10);

            struct sigaction runOneTickAction;
            memset(&runOneTickAction, 0, sizeof(runOneTickAction));
            runOneTickAction.sa_handler = handleRunOneTick;
            sigemptyset(&runOneTickAction.sa_mask);
            runOneTickAction.sa_flags = SA_RESTART;
            sigaction(SIGUSR1, &runOneTickAction, NULL);

            while (1) pause();
        }

        // parent init
        processTable[i].processId = pid;
        processTable[i].state = PROCESS_READY;
        processTable[i].remainingTimeQuantum = initialTimeQuantum;
        processTable[i].wakeUpTick = -1;

        processTable[i].isInReadyQueue = 0;
        processTable[i].readyQueueEnterTick = 0;
        processTable[i].totalReadyQueueWaitingTicks = 0;

        // 처음 readyQueue에 들어감: currentTick = 0
        enqueueToReadyQueue(&readyQueue, processTable, i, 0);
    }

    int currentlyRunningIndex = -1;
    long currentTick = 0;

    // 문맥 교환 횟수(프로세스 A -> B로 바뀐 횟수)
    long totalContextSwitches = 0;
    int lastCpuOwnerIndex = -1; // 직전에 CPU를 사용하던 프로세스(초기: 없음)

    while (isAnyProcessAlive(processTable) && !stopScheduler) {
        /* sleepingQueue에서 깨어날 프로세스 -> readyQueue 또는 zeroQuantumQueue */
        while (!isWakeUpHeapEmpty(&sleepingQueue)) {
            int soonestWakeIndex = peekWakeUpHeapTop(&sleepingQueue);
            if (processTable[soonestWakeIndex].wakeUpTick > currentTick) break;

            (void)popWakeUpHeap(&sleepingQueue);

            processTable[soonestWakeIndex].wakeUpTick = -1;
            processTable[soonestWakeIndex].state = PROCESS_READY;

            if (processTable[soonestWakeIndex].remainingTimeQuantum > 0) {
                enqueueToReadyQueue(&readyQueue, processTable, soonestWakeIndex, currentTick);
            } else {
                enqueueIndex(&zeroQuantumQueue, soonestWakeIndex);
            }
        }

        /* DONE 제외 전부 타임퀀텀 0이면 reset */
        if (areAllTimeQuantumsZero(processTable)) {
            for (int i = 0; i < CHILD_PROCESS_COUNT; i++) {
                if (processTable[i].state != PROCESS_DONE) {
                    processTable[i].remainingTimeQuantum = initialTimeQuantum;
                }
            }
            moveZeroQuantumQueueToReadyQueue(&zeroQuantumQueue, &readyQueue, processTable, currentTick);
        }

        /* 실행할 프로세스 선택: readyQueue에서 RR 순서대로 */
        if (currentlyRunningIndex == -1) {
            while (!isQueueEmpty(&readyQueue)) {
                int candidateIndex = dequeueIndex(&readyQueue);

                // readyQueue에서 나오는 순간: readyQueue 체류시간 누적
                onDequeuedFromReadyQueue(processTable, candidateIndex, currentTick);

                if (processTable[candidateIndex].state == PROCESS_READY &&
                    processTable[candidateIndex].remainingTimeQuantum > 0) {

                    // Context Switch 기록(프로세스 A -> B)
                    // 초기 첫 디스패치(lastCpuOwnerIndex == -1)는 제외
                    if (lastCpuOwnerIndex != -1 && lastCpuOwnerIndex != candidateIndex) {
                        totalContextSwitches++;
                    }

                    currentlyRunningIndex = candidateIndex;
                    processTable[candidateIndex].state = PROCESS_RUNNING;

                    // 새로 CPU를 주는 대상이 확정되었으니 lastCpuOwner 갱신
                    lastCpuOwnerIndex = candidateIndex;

                    break;
                } else {
                    if (processTable[candidateIndex].state == PROCESS_READY &&
                        processTable[candidateIndex].remainingTimeQuantum == 0) {
                        enqueueIndex(&zeroQuantumQueue, candidateIndex);
                    }
                }
            }
        }

        /* 1 tick 실행: running에게 SIGUSR1 + 타임퀀텀 감소 */
        if (currentlyRunningIndex != -1) {
            kill(processTable[currentlyRunningIndex].processId, SIGUSR1);
            processTable[currentlyRunningIndex].remainingTimeQuantum--;
        }

        /* 자식 종료 reap */
        if (shouldReapExitedChildren) {
            shouldReapExitedChildren = 0;
            while (1) {
                int status;
                pid_t exitedPid = waitpid(-1, &status, WNOHANG);
                if (exitedPid <= 0) break;

                int exitedIndex = findPcbIndexByPid(processTable, exitedPid);
                if (exitedIndex >= 0 && processTable[exitedIndex].state != PROCESS_DONE) {
                    processTable[exitedIndex].state = PROCESS_DONE;
                    if (currentlyRunningIndex == exitedIndex) currentlyRunningIndex = -1;

                    processTable[exitedIndex].isInReadyQueue = 0;
                }
            }
        }

        /* I/O 요청 처리: sleepingQueue에 삽입 */
        if (ioRequestedProcessId != 0) {
            pid_t requesterPid = (pid_t)ioRequestedProcessId;
            ioRequestedProcessId = 0;

            int requesterIndex = findPcbIndexByPid(processTable, requesterPid);
            if (requesterIndex >= 0 && processTable[requesterIndex].state != PROCESS_DONE) {
                if (currentlyRunningIndex == requesterIndex) currentlyRunningIndex = -1;

                processTable[requesterIndex].state = PROCESS_SLEEPING;

                int ioWaitingTicks = getRandomInclusive(1, 5);
                processTable[requesterIndex].wakeUpTick = (int)(currentTick + ioWaitingTicks);
                pushWakeUpHeap(&sleepingQueue, requesterIndex);
            }
        }

        /* 타임퀀텀 0이면 zeroQuantumQueue로 */
        if (currentlyRunningIndex != -1) {
            if (processTable[currentlyRunningIndex].remainingTimeQuantum <= 0) {
                processTable[currentlyRunningIndex].state = PROCESS_READY;
                enqueueIndex(&zeroQuantumQueue, currentlyRunningIndex);
                currentlyRunningIndex = -1;
            }
        }

        currentTick++;
        usleep((useconds_t)tickIntervalUsec);
    }

    /* SIGINT로 중단 시 자식 정리 */
    if (stopScheduler) {
        for (int i = 0; i < CHILD_PROCESS_COUNT; i++) {
            if (processTable[i].state != PROCESS_DONE) {
                kill(processTable[i].processId, SIGKILL);
            }
        }
        while (waitpid(-1, NULL, 0) > 0) {}
        fprintf(stderr, "\n[Stopped by SIGINT]\n");
    }

    /* 결과 집계: 평균 readyQueue 체류시간 */
    long totalWaitingTicksAll = 0;
    long totalWaitingTicksFinished = 0;
    int finishedProcessCount = 0;

    for (int i = 0; i < CHILD_PROCESS_COUNT; i++) {
        totalWaitingTicksAll += processTable[i].totalReadyQueueWaitingTicks;

        if (processTable[i].state == PROCESS_DONE) {
            finishedProcessCount++;
            totalWaitingTicksFinished += processTable[i].totalReadyQueueWaitingTicks;
        }
    }

    double averageWaitingTicksAll =
        (double)totalWaitingTicksAll / (double)CHILD_PROCESS_COUNT;

    double averageWaitingTicksFinishedOnly =
        (finishedProcessCount > 0)
            ? (double)totalWaitingTicksFinished / (double)finishedProcessCount
            : 0.0;

    printf("TIME_QUANTUM=%d, TICK_USEC=%d, SEED=%u, totalTicks=%ld\n",
           initialTimeQuantum, tickIntervalUsec, seed, currentTick);

    printf("Average READY-QUEUE waiting time (ticks) [all %d]: %.2f\n",
           CHILD_PROCESS_COUNT, averageWaitingTicksAll);

    printf("Average READY-QUEUE waiting time (ticks) [finished %d]: %.2f\n",
           finishedProcessCount, averageWaitingTicksFinishedOnly);

    printf("Total Context Switches (process->process): %ld\n", totalContextSwitches);

    for (int i = 0; i < CHILD_PROCESS_COUNT; i++) {
        printf("Process[%02d] pid=%d state=%d | readyQueueWait=%ld\n",
               i,
               (int)processTable[i].processId,
               (int)processTable[i].state,
               processTable[i].totalReadyQueueWaitingTicks);
    }

    return 0;
}

