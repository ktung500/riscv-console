#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "RVCOS.h"

// QUESTIONS???
// How to change threadarray to dynamic allocation? (It holds all the TCB's)
// How to dynamically do queues? (Do I use realloc)
// How to check if in writetext if it is VT-100 sequence? (Do I first check: '0x1B'?)
// 
//#define DEBUG(str)    WriteString2((str),strlen(str))


volatile int global;
volatile int cursor; // used for global cursor
volatile int tick_count;
volatile uint32_t controller_status = 0;
volatile uint32_t *saved_sp;
uint32_t *app_global_p;
#define MTIMECMP_LOW    (*((volatile uint32_t *)0x40000010))
#define MTIMECMP_HIGH   (*((volatile uint32_t *)0x40000014))
#define MIN_ALLOCATION_COUNT        0x40
typedef void (*TFunctionPointer)(void);
void enter_cartridge(void);
uint32_t call_th_ent(void *param, TThreadEntry entry, uint32_t *gp);
TThreadID get_tp(void);
void set_tp(TThreadID tid);
void ContextSwitch(volatile uint32_t **oldsp, volatile uint32_t *newsp);
#define CART_STAT_REG (*(volatile uint32_t *)0x4000001C)
#define CONTROLLER_STATUS_REG (*(volatile uint32_t*)0x40000018) // base address of the Multi-Button Controller Status Register
volatile char *VIDEO_MEMORY = (volatile char *)(0x50000000 + 0xFE800);  // taken from riscv-example, main code
volatile int numSleepers;
volatile int sleeperCursor;
volatile TThreadID global_tid_nums = 2;  // should only be 2-256
volatile int num_mutex = 0;
struct TCB** threadArray;
volatile int num_of_threads = 0;
int threadArraySize = 256; // If it fills up, double the size
volatile int num_mem_pool = 0;
struct MPCB** memPoolArray;
volatile TMemoryPoolID global_mpid_nums = 1; // system memory pool is 0

struct ReadyQ{
    int* queue;
    int front;
    int rear;
    int size;
};

struct Mutex** mutexArray;
struct Mutex{
    struct PrioQ *pq;
    TMutexID mxid;
    int unlocked; // check if mutex can be acquired, == 1 is unlocked, == 0 is locked
    TThreadID holder; // id of thread thats holding
} Mutex;

// Memory Pool Start -----------------------------------------------------------------------------------------------------------------------------

typedef struct FSSNode_TAG FSSNode, *FSSNodeRef;

struct FSSNode_TAG{
    struct FSSNode_TAG *next;
};

typedef struct{
    int count;
    int structureSize;
    FSSNodeRef firstFree;
} FSSAllocator, *FSSAllocatorRef;

typedef struct{
    int DSize;
    void *DBase;
} FreeChunk, *FreeChunkRef; 

FSSAllocator FreeChunkAllocator;
int SuspendAllocationOfFreeChunks = 0;
FreeChunk InitialFreeChunks[5];

FreeChunkRef AllocateFreeChunk(void);
void DeallocateFreeChunk(FreeChunkRef chunk);

void *MemoryAlloc(int size);

void FSSAllocatorInit(FSSAllocatorRef alloc, int size);
void *FSSAllocate(FSSAllocatorRef alloc);
void FSSDeallocate(FSSAllocatorRef alloc, void *obj);

typedef struct FreeNode_TAG FreeNode, *FreeNodeRef;

struct FreeNode_TAG{
    struct FreeNode_TAG *next;
    uint8_t base;
    uint32_t size;
};

struct MPCB{
    TMemoryPoolID mpid;
    uint8_t base;
    uint32_t size;
    uint32_t freeSize;
    FreeNodeRef firstFree;
    FreeNodeRef allocList;
};

void *MemoryAlloc(int size){
    AllocateFreeChunk();
    return malloc(size);
}

void FSSAllocatorInit(FSSAllocatorRef alloc, int size){
    alloc->count = 0;
    alloc->structureSize = size;
    alloc->firstFree = NULL;
}

void *FSSAllocate(FSSAllocatorRef alloc){
    if(!alloc->count){
        alloc->firstFree = MemoryAlloc(alloc->structureSize * MIN_ALLOCATION_COUNT);
        FSSNodeRef Current = alloc->firstFree;
        for(int Index = 0; Index < MIN_ALLOCATION_COUNT; Index++){
            if(Index + 1 < MIN_ALLOCATION_COUNT){
                Current->next = (FSSNodeRef)((uint8_t *)Current + alloc->structureSize);
                Current = Current->next;
            }
            else{
                Current->next = NULL;
            }
        }
        alloc->count = MIN_ALLOCATION_COUNT;
    }
    FSSNodeRef NewStruct = alloc->firstFree;
    alloc->firstFree = alloc->firstFree->next;
    alloc->count--;
    return NewStruct;
}

void FSSDeallocate(FSSAllocatorRef alloc, void *obj){
    FSSNodeRef OldStruct = (FSSNodeRef)obj;
    alloc->count++;
    OldStruct->next = alloc->firstFree;
    alloc->firstFree = OldStruct;
}

FreeChunkRef AllocateFreeChunk(void){
    if(3 > FreeChunkAllocator.count && !SuspendAllocationOfFreeChunks){
        SuspendAllocationOfFreeChunks = 1;
        uint8_t *Ptr = MemoryAlloc(FreeChunkAllocator.structureSize * MIN_ALLOCATION_COUNT);
        for(int Index = 0; Index < MIN_ALLOCATION_COUNT; Index++){
            FSSDeallocate(&FreeChunkAllocator,Ptr + Index * FreeChunkAllocator.structureSize);
        }
        SuspendAllocationOfFreeChunks = 0;
    }
    return (FreeChunkRef)FSSAllocate(&FreeChunkAllocator);
}

void DeallocateFreeChunk(FreeChunkRef chunk){
    FSSDeallocate(&FreeChunkAllocator,(void *)chunk);
}



// Memory Pool End ---------------------------------------------------------------------------------------------------------------------------------------


struct ReadyQ* createReadyQ(int size){
    struct ReadyQ *Q;
    RVCMemoryPoolAllocate(0, sizeof(struct ReadyQ), (void**)&Q);
    Q->front = 0;
    Q->rear = -1;
    Q->size = 0;
    RVCMemoryPoolAllocate(0, sizeof(int)*size, (void**)&Q->queue);
    return Q;
}


struct PrioQ {
    int size;
    int* highPQ;
    int highFront;
    int highRear;
    int highSize;
    int* norPQ;
    int norFront;
    int norRear;
    int norSize;
    int* lowPQ;
    int lowFront;
    int lowRear;
    int lowSize;
}PrioQ;

struct PrioQ *scheduleQ;
struct ReadyQ *waiterQ;
struct ReadyQ *sleeperQ;
struct ReadyQ *writerQ;

struct PrioQ* createQueue(int maxSize)
{
        /* Create a Queue */
        struct PrioQ *Q;
        RVCMemoryPoolAllocate(0, sizeof(struct PrioQ), (void**)&Q);
        Q -> highFront = 0;
        Q -> highRear = -1;
        Q -> highSize = 0;
        RVCMemoryPoolAllocate(0, sizeof(int)*maxSize, (void**)&Q->highPQ);
        Q -> norFront = 0;
        Q -> norRear = -1;
        Q -> norSize = 0;
        RVCMemoryPoolAllocate(0, sizeof(int)*maxSize, (void**)&Q->norPQ);
        Q -> lowFront = 0;
        Q -> lowRear = -1;
        Q -> lowSize = 0;
        RVCMemoryPoolAllocate(0, sizeof(int)*maxSize, (void**)&Q->lowPQ);
        Q -> size = 0;
        /* Return the pointer */
        return Q;
}

void insertRQ(struct ReadyQ *Q, int tid){
    //RVCWriteText("ins waiter\n", 11);
     if(Q->size != 256) {
            if(Q->rear == 255) {
                Q->rear = -1;
            }

            Q->queue[++Q->rear] = tid;
            Q->size++;
        }
}

int removeRQ(struct ReadyQ *Q){
    //RVCWriteText("rem waiter\n", 11);
    int data = -1;
    if (Q->size > 0) {
            data = Q->queue[Q->front++];
        }
        if(Q->front == 256) {
            Q->front = 0;
        }
        Q->size--;
    return data;
}

//void insert(int data, int* PQ, int front, int rear, int size ) {
void insert(struct PrioQ *Q, int data, TThreadPriority priority) {
    Q->size++;
    if (priority == RVCOS_THREAD_PRIORITY_HIGH){
        if(Q->highSize != 256) {
            if(Q->highRear == 255) {
                Q->highRear = -1;
            }

            Q->highPQ[++(Q->highRear)] = data;
            Q->highSize++;
            //RVCWriteText("Ins high\n", 9);
        }
    } else if (priority == RVCOS_THREAD_PRIORITY_NORMAL){
        if(Q->norSize != 256) {
            if(Q->norRear == 255) {
                Q->norRear = -1;
            }

            Q->norPQ[++(Q->norRear)] = data;
            Q->norSize++;
            //RVCWriteText("Ins nor\n", 8);
        }
    } else if (priority == RVCOS_THREAD_PRIORITY_LOW){
        if(Q->lowSize != 256) {
            if(Q->lowRear == 255) {
                Q->lowRear = -1;
            }

            Q->lowPQ[++(Q->lowRear)] = data;
            Q->lowSize++;
            //RVCWriteText("Ins low\n", 8);
        }
    }
}

int removeData(struct PrioQ *Q, TThreadPriority prio) {
    int data = 1;
    Q->size--;
    if (prio == RVCOS_THREAD_PRIORITY_HIGH) {
        if (Q->highSize > 0) {
            data = Q->highPQ[(Q->highFront)++];
        } else {
            return -1;
        }
        if(Q->highFront == 256) {
            Q->highFront = 0;
        }
        Q->highSize--;
    } else if (prio == RVCOS_THREAD_PRIORITY_NORMAL) {
        if (Q->norSize > 0) {
            data = Q->norPQ[(Q->norFront)++];
        } else {
            return -1;
        }
        if(Q->norFront == 256) {
            Q->norFront = 0;
        }
        Q->norSize--;
    } else if (prio == RVCOS_THREAD_PRIORITY_LOW) {
        if (Q->lowSize > 0) {
            data = Q->lowPQ[(Q->lowFront)++];
        } else {
            return -1;
        }
        if(Q->lowFront == 256) {
            Q->lowFront = 0;
        }
        Q->lowSize--;
    } else {
        return 1;
    }
    return data;
}
// all queue functions taken from https://www.geeksforgeeks.org/queue-set-1introduction-and-array-implementation/


struct TCB{
    TThreadID tid;
    uint32_t *gp;
    TThreadState state; // different states: running, ready, dead, waiting, created
    TThreadPriority priority; // different priorities: high, normal, low
    //int pid;
    uint32_t *sp;
    TMemorySize memsize;
    TThreadEntry entry;
    void *param;
    int ticks;
    TThreadID wait_id;
    TThreadReturn ret_val;
    uint8_t *stack_base; // return value of malloc
    const TTextCharacter *buffer;
    TMemorySize writesize;
};

void enqueueThread(struct TCB* thread);
void schedule();

int getTState(struct TCB* thread){
    return thread->state;
}

int getTPrio(struct TCB* thread){
    return thread->priority;
}

int getTID(struct TCB* thread){
    return thread->tid;
}

uint32_t *init_Stack(uint32_t* sp, TThreadEntry function, uint32_t param, uint32_t tp){
    sp--;
    *sp = (uint32_t)function; //sw      ra,48(sp)
    sp--;
    *sp = tp;//sw      tp,44(sp)  this will hold the current thread's id
    sp--;
    *sp = 0;//sw      t0,40(sp)
    sp--;
    *sp = 0;//sw      t1,36(sp)
    sp--;
    *sp = 0;//sw      t2,32(sp)
    sp--;
    *sp = 0;//sw      s0,28(sp)
    sp--;
    *sp = 0;//sw      s1,24(sp)
    sp--;
    *sp = param;//sw      a0,20(sp)
    sp--;
    *sp = 0;//sw      a1,16(sp)
    sp--;
    *sp = 0;//sw      a2,12(sp)
    sp--;
    *sp = 0;//sw      a3,8(sp)
    sp--;
    *sp = 0;//sw      a4,4(sp)
    sp--;
    *sp = 0;//sw      a5,0(sp)
    return sp;
}

TThreadEntry idle(){
    //RVCWriteText("idle\n", 5);
    asm volatile ("csrw mie, %0" : : "r"(0x888));   // Enable all interrupt soruces: csr_write_mie(0x888);
    asm volatile ("csrsi mstatus, 0x8");            // Global interrupt enable: csr_enable_interrupts()
    while(1);
}

void* skeleton(TThreadID thread_id){
    struct TCB* currThread = threadArray[thread_id];
    TThreadEntry entry = currThread->entry;
    void* param = currThread->param;
    asm volatile ("csrw mie, %0" : : "r"(0x888));   // Enable all interrupt soruces: csr_write_mie(0x888);
    asm volatile ("csrsi mstatus, 0x8");            // Global interrupt enable: csr_enable_interrupts()
    //MTIMECMP_LOW = 1;
    //MTIMECMP_HIGH = 0;
    // call entry(param) but make sure to switch the gp right before the call


    currThread->ret_val = call_th_ent(param, entry, app_global_p);
    asm volatile ("csrci mstatus, 0x8");
    RVCThreadTerminate(thread_id, currThread->ret_val); //(TThreadReturn)
    // Disable intterupts before terminate
    //Threadterminate;

}

TStatus RVCInitialize(uint32_t *gp) {
    RVCMemoryPoolAllocate(0, threadArraySize * sizeof(void *), (void**)&threadArray);
    for (int i = 0; i < threadArraySize; i++) {
        threadArray[i] = NULL;
    }
    scheduleQ = createQueue(256); // grow if we hit limit
    waiterQ = createReadyQ(256);
    sleeperQ = createReadyQ(256);
    writerQ = createReadyQ(256);
    RVCMemoryPoolAllocate(0, 256 * sizeof(void *), (void**)&mutexArray);
    for (int i = 0; i < 256; i++) {
        mutexArray[i] = NULL;
    }
    RVCMemoryPoolAllocate(0, 256 * sizeof(void *), (void**)&memPoolArray);
    for (int i = 0; i < 256; i++) {
        memPoolArray[i] = NULL;
    }
    struct TCB* mainThread;  // initializing TCB of main thread
    RVCMemoryPoolAllocate(0, sizeof(struct TCB), (void**)&mainThread);
    // FSSAllocator TCBPool;
    // FSSAllocatorInit(&TCBPool, sizeof(struct TCB));
    
    mainThread->tid = 0;
    mainThread->state = RVCOS_THREAD_STATE_RUNNING;
    mainThread->priority = RVCOS_THREAD_PRIORITY_NORMAL;
    //mainThread->pid = -1; // main thread has no parent so set to -1
    threadArray[0] = mainThread;
    num_of_threads += 1;
    set_tp(mainThread->tid);


    
    struct TCB* idleThread;
    RVCMemoryPoolAllocate(0, sizeof(struct TCB), (void**)&idleThread);
    idleThread->tid = 1;
    idleThread->state = RVCOS_THREAD_STATE_READY;   // idle thread needs to be in ready state
    idleThread->priority = 0; // RVCOS_THREAD_PRIORITY_LOWEST
    threadArray[1] = idleThread;
    num_of_threads += 1;
    idleThread->entry = (TThreadEntry)idle;
    uint8_t *idle_sb;
    //RVCMemoryPoolAllocate(0, 1024, (void**)&idleThread->stack_base);
    RVCMemoryPoolAllocate(0, 256, (void**)&idle_sb);
    idleThread->stack_base = idle_sb;
    idleThread->sp = init_Stack((uint32_t*)(idleThread->stack_base + 256), (TThreadEntry)(idleThread->entry), (uint32_t)(idleThread->param), idleThread->tid);
    
    app_global_p = gp;
    if (app_global_p == 0) {
    // Failure since it didn't change global variable
        return RVCOS_STATUS_FAILURE;
    } else {
    // return success
        return RVCOS_STATUS_SUCCESS;
    }
}

TStatus RVCWriteText(const TTextCharacter *buffer, TMemorySize writesize){
    if (buffer == NULL){
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    else{
        struct TCB* currentThread = threadArray[get_tp()];
        currentThread->buffer = buffer;
        currentThread->writesize = writesize;
        currentThread->state = RVCOS_THREAD_STATE_WAITING;
        insertRQ(writerQ, currentThread->tid);
        // Insert current before scheduling
        schedule();
        return RVCOS_STATUS_SUCCESS;
    }
}


TStatus RVCWriteText1(const TTextCharacter *buffer, TMemorySize writesize){
    if (buffer == NULL){
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    else{
        //write out writesize characters to the location specified by buffer
        if (cursor < 2304) { // 2304 = 64 columns x 36 rows

            for (int i = 0; i < (int)writesize; i++) {
                char c = buffer[i];
                //VIDEO_MEMORY[cursor] = ' ';
                if (c == '\x1B') {
                    i++;
                    if (i > (int)writesize) {
                        break;
                    }
                    char c = buffer[i];
                    if (c == '[') {
                        i++;
                        if (i > (int)writesize) {
                            break;
                        }
                        char c = buffer[i];
                        if (c == 'A') {
                            if (cursor >= 0x40) {
                                cursor -= 0x40;
                            }
                        } else if (c == 'B') {
                            cursor += 0x40;
                            if (cursor >= 2304) {
                                memmove(VIDEO_MEMORY, VIDEO_MEMORY + 0x40, 64*35);
                                memset(VIDEO_MEMORY + 64*35, 0, 64);
                                cursor -= 0x40;
                            }
                        } else if (c == 'C') {
                            if (cursor % 0x40 != 63) { // only move right if not at the right of screen
                                cursor += 1;
                            }
                        } else if (c == 'D') {
                            if (cursor % 0x40 != 0) { // only move left if not at left of screen
                                cursor -= 1;
                            }
                        } else if (c == 'H') {
                            cursor = 0;
                        } else if (c == '2') {
                            i++;
                            if (i > (int)writesize) {
                                break;
                            }
                            char c = buffer[i];
                            if (c == 'J') {
                                // Erase screen (zero out video_memory)
                                memset(VIDEO_MEMORY, 0, 2304);
                            }
                        } else {
                            int ln = (int)c - '0';
                            i++;
                            if (i > (int)writesize) {
                                break;
                            }
                            c = buffer[i];
                            if (c == ';') {
                                i++;
                                if (i > (int)writesize) {
                                    break;
                                }
                                c = buffer[i];
                                int col = (int)c - '0';
                                i++;
                                if (i > (int)writesize) {
                                    break;
                                }
                                c = buffer[i];
                                if (c == 'H') {
                                    // cursor = 65;
                                    // VIDEO_MEMORY[cursor] = ln;
                                    // cursor ++;
                                    // VIDEO_MEMORY[cursor] = col;
                                    // cursor ++;
                                    cursor = (64 * ln) + col;
                                    // cursor = 65;
                                }
                            }
                        }
                            // erase screen, don't move cursor
                    }
                }
                else if (c == '\n') {
                    cursor += 0x40;
                    cursor = cursor & ~0x3F;
                    //VIDEO_MEMORY[cursor] = c;
                    if (cursor >= 2304) {
                        memmove(VIDEO_MEMORY, VIDEO_MEMORY + 0x40, 64*35);
                        memset(VIDEO_MEMORY + 64*35, 0, 64);
                        cursor -= 0x40;
                    }
                } else if(c == '\b') {
                    cursor -= 1;
                    //VIDEO_MEMORY[cursor] = c;
                } else {
                    VIDEO_MEMORY[cursor] = c;
                    cursor++;
                }
            }
            //VIDEO_MEMORY[global] = *buffer;
            //global = global + writesize;
        }
        // if there are errors try casting (int)
        return RVCOS_STATUS_SUCCESS;
    }
}

// taken from CartridgeThreadWait
void WriteString(const char *str){
    const char *Ptr = str;
    while(*Ptr){
        Ptr++;
    }
    RVCWriteText1(str,Ptr-str);
}

TStatus RVCReadController(SControllerStatusRef statusref){
    if (statusref == NULL){
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    else{
        *(uint32_t *)statusref = CONTROLLER_STATUS_REG; // the struct is 32 bits and aligned with the bits in the register
        return RVCOS_STATUS_SUCCESS;                    // from Piazza post question @410
    }
}

TStatus RVCThreadCreate(TThreadEntry entry, void *param, TMemorySize memsize,
                        TThreadPriority prio, TThreadIDRef tid){
    if (entry == NULL || tid == NULL){
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    else{
        //void* newThreadStack[memsize];
        if(num_of_threads > threadArraySize){  // number of threads exceeeds current size of array
            // double size
            threadArraySize *= 2;
            int newSize = threadArraySize;
            //threadArray = realloc(threadArray, threadArraySize * sizeof(struct TCB));
            struct TCB **temp;
            RVCMemoryPoolAllocate(0, threadArraySize * sizeof(void *), (void**)temp);
            for (int i = 0; i < threadArraySize; i++) {
                threadArray[i] = NULL;
            }
            for (int i=0; i < threadArraySize; i++) {
                temp[i] = threadArray[i];
            }
            RVCMemoryDeallocate(threadArray);
            threadArray = temp;
            threadArraySize = newSize;
            // need to implement a realloc function using memory pools
            // return RVCOS_STATUS_ERROR_INSUFFICIENT_RESOURCES;
        }
        else{
            struct TCB* newThread; // initializing TCB of a thread
            RVCMemoryPoolAllocate(0, sizeof(struct TCB), (void**)&newThread);
            uint8_t *sb;
            RVCMemoryPoolAllocate(0, memsize, (void**)&sb);
            newThread->entry = entry;
            newThread->param = param;
            newThread->memsize = memsize;
            newThread->tid = global_tid_nums;
            newThread->stack_base = sb;
            *tid = global_tid_nums;
            newThread->state = RVCOS_THREAD_STATE_CREATED;
            newThread->priority = prio;
            threadArray[global_tid_nums] = newThread;
            num_of_threads++;
            global_tid_nums++;  // starts at 2, since global_tid_nums is initialized to 2
        }
    }
    return RVCOS_STATUS_SUCCESS;
}

TStatus RVCThreadDelete(TThreadID thread){
    if (threadArray[thread] == NULL){
        return RVCOS_STATUS_ERROR_INVALID_ID;
    }
    struct TCB* currThread = threadArray[thread];
    if (currThread->state != RVCOS_THREAD_STATE_DEAD){
        return  RVCOS_STATUS_ERROR_INVALID_STATE;
    }
    else{
        num_of_threads--;
        RVCMemoryPoolDeallocate(0, currThread->stack_base);
        RVCMemoryPoolDeallocate(0, currThread);
        threadArray[thread] = NULL;
        return RVCOS_STATUS_SUCCESS;
    }
}

TStatus RVCThreadActivate(TThreadID thread){   // we handle scheduling and context switching in here
    if (threadArray[thread] == NULL){
        return RVCOS_STATUS_ERROR_INVALID_ID;
    }
    struct TCB* actThread = threadArray[thread];
    if (actThread->state != RVCOS_THREAD_STATE_DEAD && actThread->state != RVCOS_THREAD_STATE_CREATED){
        return  RVCOS_STATUS_ERROR_INVALID_STATE;
    }
    else{
        actThread->sp = init_Stack((uint32_t *)(actThread->stack_base + actThread->memsize), (TThreadEntry)skeleton, actThread->tid, thread);
        //currThread->sp = init_Stack((uint32_t*)(currThread->stack_base + currThread->memsize), &skeleton, currThread->tid, thread); // initializes stack/ activates thread
        actThread->state = RVCOS_THREAD_STATE_READY;
        enqueueThread(actThread);
        struct TCB* currentThread = threadArray[get_tp()];
        if(actThread->priority > currentThread->priority){
            currentThread->state = RVCOS_THREAD_STATE_READY;
            enqueueThread(currentThread);
            schedule();
        }
        // call scheduler
        return RVCOS_STATUS_SUCCESS;
    }
}

void enqueueThread(struct TCB* thread){
    if(thread->priority == RVCOS_THREAD_PRIORITY_LOW){
        insert(scheduleQ, thread->tid, RVCOS_THREAD_PRIORITY_LOW);
    }
    else if(thread->priority == RVCOS_THREAD_PRIORITY_NORMAL){
        insert(scheduleQ, thread->tid, RVCOS_THREAD_PRIORITY_NORMAL);
    }
    else if(thread->priority == RVCOS_THREAD_PRIORITY_HIGH) {
        insert(scheduleQ, thread->tid, RVCOS_THREAD_PRIORITY_HIGH);
    }
}

TStatus RVCThreadTerminate(TThreadID thread, TThreadReturn returnval) {
    if (threadArray[thread] == NULL){
        return RVCOS_STATUS_ERROR_INVALID_ID;
    }
    struct TCB* currThread = threadArray[thread];
    if (currThread->state == RVCOS_THREAD_STATE_DEAD || currThread->state == RVCOS_THREAD_STATE_CREATED){
        return  RVCOS_STATUS_ERROR_INVALID_STATE;
    }
    // WriteString("\nthread terminating: ");
    // char buff[20];
    // uint32_t id = thread;
    // itoa(id, buff, 10);
    // WriteString(buff);
    // WriteString("\nthread retval: ");
    // char buff1[20];
    // uint32_t id1 = returnval;
    // itoa(id1, buff1, 10);
    // WriteString(buff1);
    //WriteString("\n");
    currThread->state = RVCOS_THREAD_STATE_DEAD;
    currThread->ret_val = returnval;
    //  WriteString("\nthread state: ");
    // char buff2[20];
    // uint32_t id2 = currThread->state;
    // itoa(id2, buff2, 10);
    // WriteString(buff2);
    // WriteString("\n");
    // if there are waiters
    if(waiterQ->size != 0){
        int flag = 0;
        for(int i = 0; i < waiterQ->size; i++){
            int waiterTID = removeRQ(waiterQ);
            struct TCB* waiter = threadArray[waiterTID];
            // if the thread terminating is the thread that a waiter is waiting on
            if (waiter->wait_id == thread){
                waiter->ret_val = returnval;
                waiter->state = RVCOS_THREAD_STATE_READY;
                enqueueThread(waiter);
                if(waiter->priority > currThread->priority){
                    flag = 1;
                }
            }
            else{
                insertRQ(waiterQ,waiter->tid);
            }
        }
        if(flag == 1){
            schedule();
        }
    }
    //If the thread terminating is the current running thread, then you will definitely need to schedule.
    if(threadArray[get_tp()] == currThread){
        //RVCWriteText1("current thread dead\n",20);
        schedule();
    }
    return RVCOS_STATUS_SUCCESS;
}

TStatus RVCThreadWait(TThreadID thread, TThreadReturnRef returnref, TTick timeout) {
    if (threadArray[thread] == NULL){
        return RVCOS_STATUS_ERROR_INVALID_ID;
    }
    else if(returnref == NULL){
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    struct TCB* currThread = threadArray[get_tp()];
    struct TCB* waitThread = threadArray[thread];
    
    if (timeout == RVCOS_TIMEOUT_IMMEDIATE){
        //the current returns immediately regardless if the thread has terminated
        if(waitThread->state != RVCOS_THREAD_STATE_DEAD){
            return RVCOS_STATUS_FAILURE;
        }
        else{
            *returnref = (TThreadReturn)currThread->ret_val;
            return RVCOS_STATUS_SUCCESS;
        }
    }
    else if (timeout == RVCOS_TIMEOUT_INFINITE){
        if (waitThread->state != RVCOS_THREAD_STATE_DEAD) {
            currThread->state = RVCOS_THREAD_STATE_WAITING;
            currThread->wait_id = thread;
            insertRQ(waiterQ,currThread->tid);
            schedule();
            *returnref = (TThreadReturn)currThread->ret_val;
            return RVCOS_STATUS_SUCCESS;
        }
        else {
            *returnref = (TThreadReturn)waitThread->ret_val; // not sure what this is for
            return RVCOS_STATUS_SUCCESS;
        }
        
    }
    else{
        currThread->ticks = timeout;
        currThread->state = RVCOS_THREAD_STATE_WAITING;
        insertRQ(sleeperQ, currThread->tid);
        numSleepers++;
        schedule();
        // schedules next thread after putting current thread to sleep
        // checks the timeout of the current thread that was put to sleep
        // if the timeout expired and the thread it was waiting on is not dead yet, return failure
        if(currThread->ticks == 0 && waitThread->state != RVCOS_THREAD_STATE_DEAD){
            return RVCOS_STATUS_FAILURE;
        }
        if (waitThread->state != RVCOS_THREAD_STATE_DEAD) {
            currThread->state = RVCOS_THREAD_STATE_WAITING;
            currThread->wait_id = thread;
            insertRQ(waiterQ,currThread->tid);
            schedule();
            *returnref = (TThreadReturn)currThread->ret_val;
            return RVCOS_STATUS_SUCCESS;
        }
        //*returnref = (TThreadReturn)waitThread->ret_val;   
        return RVCOS_STATUS_SUCCESS;
    }
}

TStatus RVCThreadID(TThreadIDRef threadref){
    if (threadref == NULL){
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    *threadref = get_tp();
    return RVCOS_STATUS_SUCCESS;
}

TStatus RVCThreadState(TThreadID thread, TThreadStateRef state){
    if (state == NULL){
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    else if (threadArray[thread] == NULL){
        return RVCOS_STATUS_ERROR_INVALID_ID;
    }
    *state = threadArray[thread]->state;
    return RVCOS_STATUS_SUCCESS;
}

void schedule(){
    struct TCB* current = threadArray[get_tp()];
    int nextTid = 0;
    struct TCB* nextT;
    if (scheduleQ->highSize != 0) {

        nextTid = removeData(scheduleQ, RVCOS_THREAD_PRIORITY_HIGH);

    } else if (scheduleQ->norSize != 0) {

        nextTid = removeData(scheduleQ, RVCOS_THREAD_PRIORITY_NORMAL);

    } else if (scheduleQ->lowSize != 0) {
        // RVCWriteText1("Schedule thread: ", 17);
        //RVCWriteText1("           ", 11);
        nextTid = removeData(scheduleQ, RVCOS_THREAD_PRIORITY_LOW);
        // char buff[1];
        // uint32_t id = nextTid;
        // itoa(nextTid, buff, 10);
        // WriteString(buff);
    } else {
        nextTid = 1;
    }
    nextT = threadArray[nextTid];
    if(current->tid != nextTid){
        nextT->state = RVCOS_THREAD_STATE_RUNNING;
        // if(current->state != RVCOS_THREAD_STATE_DEAD && current->state != RVCOS_THREAD_STATE_WAITING && nextT->state != RVCOS_THREAD_STATE_DEAD){
        //     current->state = RVCOS_THREAD_STATE_READY;
        //     //enqueueThread(current);
        // }
        
        ContextSwitch((void *)&current->sp, threadArray[nextTid]->sp);
        
    }
    else{
        current->state = RVCOS_THREAD_STATE_RUNNING;
        //nextT->state = RVCOS_THREAD_STATE_READY;
        // enqueueThread(nextT);
    }

}

TStatus RVCThreadSleep(TTick tick) {
    if (tick == RVCOS_TIMEOUT_INFINITE){
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    else if(tick == RVCOS_TIMEOUT_IMMEDIATE){
        struct TCB* current = threadArray[get_tp()];
        TThreadPriority currPrio = current->priority;
        
        int next = removeData(scheduleQ, currPrio);
        if (next == 1) {
            enqueueThread(current);
        }
        struct TCB* nextRT = threadArray[next];
        enqueueThread(nextRT);
    } else {
        struct TCB* current = threadArray[get_tp()];
        current->ticks = tick;
        current->state = RVCOS_THREAD_STATE_WAITING;
        insertRQ(sleeperQ, current->tid);
        //sleepers[sleeperCursor] = current;
        //sleeperCursor++;
        numSleepers++;
        schedule();
        return RVCOS_STATUS_SUCCESS;
    }
}

TStatus RVCTickMS(uint32_t *tickmsref) {
    if (tickmsref == NULL) {
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    *tickmsref = 2; // 2000/MTIME;
    return RVCOS_STATUS_SUCCESS;
}

TStatus RVCTickCount(TTickRef tickref) {
    if (tickref == NULL){
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER ;
    }
    else{
        *tickref = tick_count;
        return RVCOS_STATUS_SUCCESS;
    }
}

// from discussion11-05.c
// void *MemoryAlloc(int size){
//     AllocateFreeChunk();
//     return malloc(size);
// }

TStatus  RVCMemoryPoolCreate(void  *base,  TMemorySize  size,  TMemoryPoolIDRef memoryref) {
    if (base == NULL || size < 128) {
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    // struct MemAllocator *alloc;]
    struct MPCB *memPool;
    memPool->base = base;
    memPool->size = size;
    memPool->freeSize = size;
    memPool->allocList = NULL;
    memPool->firstFree = NULL;
    memPool->mpid = global_mpid_nums;
    global_mpid_nums++;
    memPoolArray[global_mpid_nums] = memPool;

    // alloc->structureSize = size;   // size of the memory pool, need to be decreased when allocating
    // alloc->base = base;
    // alloc->count = 0;
    // alloc->firstFree = NULL;
    // memPoolArray[num_mem_pool] = alloc;
    // *memoryref = num_mem_pool;
    // num_mem_pool++;
    // Store base and size and create ID
    // Upon successful creation of the memory pool, RVCMemoryPoolCreate() will return
    return RVCOS_STATUS_SUCCESS;
}

TStatus RVCMemoryPoolDelete(TMemoryPoolID memory) {

    if (memory == RVCOS_MEMORY_POOL_ID_SYSTEM) { // Or if memory is invalid memory pool
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    // if any memory has been allocated from the pool and is not deallocated
    //   return RVCOS_STATUS_ERROR_INVALID_STATE 
}

TStatus RVCMemoryPoolQuery(TMemoryPoolID memory, TMemorySizeRef bytesleft) {
    if (bytesleft == NULL) { // Or if memory is invalid memory pool
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    *bytesleft = memPoolArray[memory]->freeSize;
    return RVCOS_STATUS_SUCCESS;
}

// allocate might call allocatefreechunk
TStatus RVCMemoryPoolAllocate(TMemoryPoolID memory, TMemorySize size, void **pointer) {
    if (memory == RVCOS_MEMORY_POOL_ID_SYSTEM){
        *pointer = (void *)malloc(size);
        return RVCOS_STATUS_SUCCESS;
    }
    if (size == 0 || pointer == NULL ) { // Or if memory is invalid memory pool
        RVCWriteText1("invalid pool\n", 13);
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;

    }
    else if (memPoolArray[memory] == NULL){
        RVCWriteText1("invalid id\n", 11);
        return RVCOS_STATUS_ERROR_INVALID_ID;
    }
    else if(memPoolArray[memory]->freeSize < size){
        RVCWriteText1("no space\n", 9);
        return RVCOS_STATUS_ERROR_INSUFFICIENT_RESOURCES;
    }
    else{
        struct MPCB *currPool = memPoolArray[memory];
        uint32_t alloc_size = ((size + 63)/64) * 64;
        FreeNodeRef cur = currPool->firstFree;
        while(cur) {
            if (alloc_size <= cur->size) {
                if (alloc_size == cur->size) {
                    // pull off freelist
                    // move cur
                    // alloc list
                    // return ptr cur->base
                }
                else {
                    FreeNode *newnode;
                    newnode->base = cur->base;
                    newnode->size = alloc_size;
                    cur->base += alloc_size;
                    cur->size -= alloc_size;
                    // add newn ode to alloc
                    // return newnode->base
                }
            }
            cur = cur->next;
        }

        // if (memory == RVCOS_MEMORY_POOL_ID_SYSTEM){
        //     *pointer = (void *)malloc(size);
        //     return RVCOS_STATUS_SUCCESS;
        // }
        //else{
            
        return RVCOS_STATUS_SUCCESS;
        //}
    }
    //*pointer = (struct TCB*)malloc(size);
    //return RVCOS_STATUS_SUCCESS;
    // If the memory pool does not have sufficient memory to allocate the array of size bytes, 
    // RVCOS_STATUS_ERROR_INSUFFICIENT_RESOURCES is returned. 
}




TStatus RVCMemoryPoolDeallocate(TMemoryPoolID memory, void *pointer) {
    if (pointer == NULL) { // Or if memory is invalid memory pool
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    free(pointer);
    return RVCOS_STATUS_SUCCESS;
    //  If pointer does not specify a memory location that was previously allocated from the memory pool, 
    // RVCOS_STATUS_ERROR_INVALID_PARAMETER is returned. 
}

TStatus RVCMutexCreate(TMutexIDRef mutexref) {
    if (mutexref == NULL) {
         return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    struct Mutex *mx;
    RVCMemoryPoolAllocate(0, sizeof(struct Mutex), (void**)&mx);
    struct PrioQ *mxQueue = createQueue(256);

    mx -> pq = mxQueue;
    mx -> mxid = num_mutex;
    mx -> unlocked = 1;
    mx -> holder = NULL;
    mutexArray[num_mutex] = mx;
    *mutexref = num_mutex;
    num_mutex ++;

}

TStatus RVCMutexDelete(TMutexID mutex) {
    if(mutexArray[mutex] == NULL){
        return RVCOS_STATUS_ERROR_INVALID_ID;
    }
    else if(mutexArray[mutex]->unlocked == 0){
        return RVCOS_STATUS_ERROR_INVALID_STATE;
    }
    else{
        RVCMemoryPoolDeallocate(0, mutexArray[mutex]);
        mutexArray[mutex] = NULL;
        num_mutex--;
        return RVCOS_STATUS_SUCCESS;   
    }
}

TStatus RVCMutexQuery(TMutexID mutex, TThreadIDRef ownerref) {
    if (ownerref == NULL) {
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER; 
    }
    // If mutex is unlocked
    if (mutexArray[mutex]->unlocked) {
        return RVCOS_THREAD_ID_INVALID;
    }
    // If mutex doesn't exist
    if (mutexArray[mutex] == NULL) {
        return RVCOS_STATUS_ERROR_INVALID_ID;
    }
    *ownerref = mutexArray[mutex] -> holder;
}

TStatus RVCMutexAcquire(TMutexID mutex, TTick timeout) {
    //RVCWriteText("Acquire\n", 8);
    if (mutexArray[mutex] == NULL) {
        //RVCWriteText("Inf Null\n", 9);
        return RVCOS_STATUS_ERROR_INVALID_ID;
    }
    struct Mutex *mx = mutexArray[mutex];
    struct TCB *currThread = threadArray[get_tp()];
    // If timeout specified as IMMEDIATE, current returns immediately if mutex is locked 
    if (timeout == RVCOS_TIMEOUT_IMMEDIATE) {
        if (mx->unlocked != 1) {
            return RVCOS_STATUS_FAILURE;
        }
    }
    // If timeout is specified as INFINITE, thread will block until mutex is acquired. 
    else if (timeout == RVCOS_TIMEOUT_INFINITE) {
        //RVCWriteText("Infinite\n", 9);
        // check if mutex is unlocked
        if (mx->unlocked == 1){
            // mutex is now held by the current running thread
            mx->holder = currThread->tid;
            // lock mutex
            mx->unlocked = 0;
        }else{
            //RVCWriteText("Inf Blocking\n", 13);
            // mutex is locked and the thread will block until mutex is unlocked
            currThread->state = RVCOS_THREAD_STATE_WAITING;
            // set currthread to waiting, add thread to pq, 
            insert(mx->pq, currThread->tid, currThread->priority);
            // need to schedule the next thread cause this one is waiting for the mutex to be released
            schedule();
        }
    }
    // If  the  timeout  expires  prior  to  the  acquisition  of  the  mutex, RVCOS_STATUS_FAILURE  is  returned.  
    else{
        // how do we check if the timeout expires, need to ask in OH about this
        if (mx->unlocked == 1){
            // mutex is now held by the current running thread
            mx->holder = currThread->tid;
            // lock mutex
            mx->unlocked = 0;
        }else{
            // mutex is locked and the thread will block until mutex is unlocked
            currThread->state = RVCOS_THREAD_STATE_WAITING;
            currThread->ticks = timeout;
            insertRQ(sleeperQ, currThread->tid);
            if(currThread->ticks == 0){
                 return RVCOS_STATUS_FAILURE;
                
            }
            // set currthread to waiting, add thread to pq, 
            //insert(mx->pq, currThread->tid, currThread->priority);
            // need to schedule the next thread cause this one is waiting for the mutex to be released

            schedule();
        }
        return RVCOS_STATUS_FAILURE;
    }
    return RVCOS_STATUS_SUCCESS;
}

//RVCMutexRelease() releases the mutex specified by the mutex parameter that is currently held by 
// the running thread. Release of the mutex may cause another higher priority thread to be scheduled 
// if it acquires the newly released mutex.  
TStatus RVCMutexRelease(TMutexID mutex) {
    //RVCWriteText("Release\n", 8);
    if (mutexArray[mutex] == NULL) {
        return RVCOS_STATUS_ERROR_INVALID_ID;
    }
    else if(mutexArray[mutex]->holder != get_tp()){ 
        //  If  the  mutex  specified  by  the  mutex identifier mutex does exist, but is not currently held by the running thread, 
        // RVCOS_STATUS_ERROR_INVALID_STATE is returned. 
        //RVCWriteText("Not running thr\n", 16);
        return RVCOS_STATUS_ERROR_INVALID_STATE;
    }
    else{
        //RVCWriteText("Else\n", 5);
        struct Mutex *mx = mutexArray[mutex];
        mx->holder = NULL;
        mx->unlocked = 1;
        int nextTid = removeData(mx->pq, RVCOS_THREAD_PRIORITY_HIGH);
        if(nextTid == -1){
            nextTid = removeData(mx->pq, RVCOS_THREAD_PRIORITY_NORMAL);
            if(nextTid == -1){
               nextTid = removeData(mx->pq, RVCOS_THREAD_PRIORITY_LOW);
            }
        }
        // nothing in any of the pqs
        if(nextTid == -1){
            //RVCWriteText("nothing\n",8);
            return RVCOS_STATUS_SUCCESS;
        }
        else{
            struct TCB *nextThread = threadArray[nextTid];
            nextThread->state = RVCOS_THREAD_STATE_READY;
            enqueueThread(nextThread);
            mx->holder = nextTid;
            mx->unlocked = 0;
            //schedule();
            if (nextThread->priority > threadArray[get_tp()]->priority){
                //RVCWriteText("Schedule\n", 9);
                threadArray[get_tp()]->state = RVCOS_THREAD_STATE_READY;
                enqueueThread(threadArray[get_tp()]);
                schedule();
            }
            
            return RVCOS_STATUS_SUCCESS;
        }
    }
}


int main() {
    saved_sp = &CART_STAT_REG; // was used to see how the compiler would assign the save_sp so we could
    while(1){                      // do it in assembly in the enter_cartridge function
        if(CART_STAT_REG & 0x1){
            enter_cartridge();
            while(1){
                if(!(CART_STAT_REG&0x1)){
                    break;
                }
            }
        }
    }
    return 0;
}

uint32_t c_syscall_handler(uint32_t p1,uint32_t p2,uint32_t p3,uint32_t p4,uint32_t p5,uint32_t code){
    switch(code){
        case 0x00: return RVCInitialize((void *)p1);
        case 0x01: return RVCThreadCreate((TThreadEntry)p1, (void*)p2, p3, p4, (TThreadIDRef)p5);
        case 0x02: return RVCThreadDelete((TThreadID)p1);
        case 0x03: return RVCThreadActivate((TThreadID)p1);
        case 0x04: return RVCThreadTerminate((TThreadID)p1, p2);
        case 0x05: return RVCThreadWait((TThreadID)p1, (TThreadReturnRef)p2, (TTick)p3);
        case 0x06: return RVCThreadID((void *)p1);
        case 0x07: return RVCThreadState((TThreadID)p1, (uint32_t*)p2);
        case 0x08: return RVCThreadSleep((TThreadID)p1);
        case 0x09: return RVCTickMS((void *)p1);
        case 0x0A: return RVCTickCount((void *)p1);
        case 0x0B: return RVCWriteText((void *)p1, p2);
        case 0x0C: return RVCReadController((void *)p1);
        case 0x0D: return RVCMemoryPoolCreate(p1, p2, p3);
        case 0x0E: return RVCMemoryPoolDelete(p1);
        case 0x0F: return RVCMemoryPoolQuery(p1, p2);
        case 0x10: return RVCMemoryPoolAllocate(p1, p2, p3);
        case 0x11: return RVCMemoryPoolDeallocate(p1, p2);
        case 0x12: return RVCMutexCreate((void *)p1);
        case 0x13: return RVCMutexDelete((void *)p1);
        case 0x14: return RVCMutexQuery((void *)p1, (void *)p2);
        case 0x15: return RVCMutexAcquire((void *)p1, (void *)p2);
        case 0x16: return RVCMutexRelease ((void *)p1);

    }
    return code + 1;
}
