#include <stdint.h>
#include <stdlib.h>
#include "RVCOS.h"

volatile int global; // used for global cursor
volatile uint32_t controller_status = 0;
volatile uint32_t *saved_sp;
volatile uint32_t app_global_p;
#define MTIMECMP_LOW    (*((volatile uint32_t *)0x40000010))
#define MTIMECMP_HIGH   (*((volatile uint32_t *)0x40000014))
typedef void (*TFunctionPointer)(void);
void enter_cartridge(void);
uint32_t call_th_ent(void *param, TThreadEntry entry, uint32_t *gp);
TThreadID get_tp(void);
void ContextSwitch(volatile uint32_t **oldsp, volatile uint32_t *newsp);
#define CART_STAT_REG (*(volatile uint32_t *)0x4000001C)
#define CONTROLLER_STATUS_REG (*(volatile uint32_t*)0x40000018) // base address of the Multi-Button Controller Status Register
volatile char *VIDEO_MEMORY = (volatile char *)(0x50000000 + 0xFE800);  // taken from riscv-example, main code
volatile struct TCB* threadArray[256]; 
volatile int global_tid_nums = 1;  // should only be 2-256
volatile int num_of_threads = 0;
volatile struct Queue* highPrioQueue;
volatile struct Queue* norPrioQueue;
volatile struct Queue* lowPrioQueue;
volatile struct Queue* readyQ[4];
volatile struct Queue* waiters;

// all queue functions taken from https://www.geeksforgeeks.org/queue-set-1introduction-and-array-implementation/
struct Queue {
    int front, rear, size;
    unsigned capacity;
    int* array;
};
 
// function to create a queue 
// of given capacity.
// It initializes size of queue as 0
struct Queue* createQueue(unsigned capacity)
{
    struct Queue* queue = (struct Queue*)malloc(
        sizeof(struct Queue));
    queue->capacity = capacity;
    queue->front = queue->size = 0;
 
    // This is important, see the enqueue
    queue->rear = capacity - 1;
    queue->array = (int*)malloc(
        queue->capacity * sizeof(int));
    return queue;
}

void enqueue(struct Queue* queue, int item)
{
    if (queue->size == queue->capacity)
        return;
    queue->rear = (queue->rear + 1)
                  % queue->capacity;
    queue->array[queue->rear] = item;
    queue->size = queue->size + 1;
}

// Function to remove an item from queue.
// It changes front and size
void* dequeue(struct Queue* queue){
    if (queue->size == 0)
        return NULL;
    void* item = queue->array[queue->front];
    queue->front = (queue->front + 1)
                   % queue->capacity;
    queue->size = queue->size - 1;
    return item;
}

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
    TStatus ret_val;
    uint8_t* stack_base; // return value of malloc
};

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
    while(1);
}

void* skeleton(TThreadID thread_id){
    struct TCB* currThread = threadArray[thread_id]; 
    TThreadEntry entry = currThread->entry;
    void* param = currThread->param;
    asm volatile ("csrw mie, %0" : : "r"(0x888));   // Enable all interrupt soruces: csr_write_mie(0x888); 
    asm volatile ("csrsi mstatus, 0x8");            // Global interrupt enable: csr_enable_interrupts()   
    MTIMECMP_LOW = 1;
    MTIMECMP_HIGH = 0;
    // call entry(param) but make sure to switch the gp right before the call

    currThread->ret_val = call_th_ent(param, entry, app_global_p); 
    // Disable intterupts before terminate
    //Threadterminate;
    
}

TStatus RVCInitialize(uint32_t *gp) {
    struct TCB* mainThread = (struct TCB*)malloc(sizeof(struct TCB)); // initializing TCB of main thread
    mainThread->tid = 0;
    mainThread->state = RVCOS_THREAD_STATE_CREATED;
    mainThread->priority = RVCOS_THREAD_PRIORITY_NORMAL;
    //mainThread->pid = -1; // main thread has no parent so set to -1
    threadArray[0] = mainThread;
    
    struct TCB* idleThread = (struct TCB*)malloc(sizeof(struct TCB)); // initializing TCB of idle thread
    idleThread->tid = 1;
    idleThread->state = RVCOS_THREAD_STATE_READY;   // idle thread needs to be in ready state
    idleThread->priority = RVCOS_THREAD_PRIORITY_LOWEST;
    //idleThread->pid = -1;
    threadArray[1] = idleThread;
    idleThread->entry = idle();
    idleThread->stack_base = malloc(1024);
    idleThread->sp = init_Stack((uint32_t*)(idleThread->stack_base + 1024), idle(), idleThread->param, idleThread->tid);

    highPrioQueue = createQueue(256);
    norPrioQueue = createQueue(256);
    lowPrioQueue = createQueue(256);
    waiters = createQueue(256);

    app_global_p = *gp; 
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
        //write out writesize characters to the location specified by buffer
        if (global < 2304) { // 2304 = 64 columns x 36 rows
        
            for (int i = 0; i < (int)writesize; i++) {
                char c = buffer[i];
                VIDEO_MEMORY[global] = ' ';
                if (c == '\n') {
                    global += 0x40;
                    global -= 1;
                    VIDEO_MEMORY[global] = c;
                } else if(c == '\b') {
                    global -= 1;
                    VIDEO_MEMORY[global] = c;
                } else {
                    VIDEO_MEMORY[global] = c;
                    global++;
                }
            }
            //VIDEO_MEMORY[global] = *buffer;
            //global = global + writesize;
        }
        // if there are errors try casting (int)
        return RVCOS_STATUS_SUCCESS;
    }
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
        if(num_of_threads > 254){  // number of threads exceeeds what is possible
            return RVCOS_STATUS_ERROR_INSUFFICIENT_RESOURCES;
        }
        else if(global_tid_nums == 256) { // need to parse through the threadArray and get the num of the first empty space
            TThreadID currThreadID; 
            for(int i = 0; i < 256; i++){
                if(threadArray[i] == NULL){
                    currThreadID = i;
                    continue;
                }
            }
            struct TCB* newThread = (struct TCB*)malloc(sizeof(struct TCB)); // initializing TCB of a thread
            newThread->stack_base = malloc(memsize); // initialize stack of memsize for the newThread
            newThread->entry = entry;
            newThread->param = param;
            newThread->memsize = memsize;
            newThread->tid = currThreadID; 
            tid = newThread->tid;
            newThread->state = RVCOS_THREAD_STATE_CREATED;
            newThread->priority = prio;
            //newThread->pid = -1; 
            threadArray[currThreadID] = newThread;
            num_of_threads++;
        }
        else{
            struct TCB* newThread = (struct TCB*)malloc(sizeof(struct TCB)); // initializing TCB of a thread
            newThread->stack_base = malloc(memsize); // initialize stack of memsize for the newThread
            newThread->entry = entry;
            newThread->param = param;
            newThread->memsize = memsize;
            newThread->tid = global_tid_nums; 
            tid = newThread->tid;
            newThread->state = RVCOS_THREAD_STATE_CREATED;
            newThread->priority = prio;
            //newThread->pid = -1; 
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
        threadArray[thread] = NULL;
        free(currThread->stack_base);
        free(currThread);
        return RVCOS_STATUS_SUCCESS; 
    }
}

TStatus RVCThreadActivate(TThreadID thread){   // we handle scheduling and context switching in here
    if (threadArray[thread] == NULL){
        return RVCOS_STATUS_ERROR_INVALID_ID;
    }
    struct TCB* currThread = threadArray[thread];
    if (currThread->state != RVCOS_THREAD_STATE_DEAD || currThread->state != RVCOS_THREAD_STATE_CREATED){
        return  RVCOS_STATUS_ERROR_INVALID_STATE;
    }
    else{
        //readyQ = createQueue(4);
        currThread->sp = init_Stack((uint32_t*)(currThread->stack_base + currThread->memsize), skeleton(currThread->tid), currThread->param, currThread->tid); // initializes stack/ activates thread
        currThread->state = RVCOS_THREAD_STATE_READY;
        enqueueThread(currThread);
        //enqueue(readyQ, highPrioQueue);
        //enqueue(readyQ, norPrioQueue);
        //enqueue(readyQ, lowPrioQueue);
        //enqueue(readyQ, threadArray[1]);  // threadArray[1] will always be the idle thread
        readyQ[0] = highPrioQueue;
        readyQ[1] = norPrioQueue;
        readyQ[2] = lowPrioQueue;
        readyQ[4] = threadArray[1];
        schedule(readyQ);
        // call scheduler
        return RVCOS_STATUS_SUCCESS; 
    }
}

void enqueueThread(struct TCB* thread){
    if(thread->priority == RVCOS_THREAD_PRIORITY_HIGH){
            enqueue(highPrioQueue, thread);
    }
    else if(thread->priority = RVCOS_THREAD_PRIORITY_NORMAL){
        enqueue(norPrioQueue, thread);
    }
    else{
        enqueue(lowPrioQueue, thread);
    }
}

TStatus RVCThreadTerminate(TThreadID thread, TThreadReturn returnval) {
    if (threadArray[thread] == NULL){
        return RVCOS_STATUS_ERROR_INVALID_ID;
    }
    struct TCB* currThread = threadArray[thread];
    if (currThread->state != RVCOS_THREAD_STATE_DEAD || currThread->state != RVCOS_THREAD_STATE_CREATED){
        return  RVCOS_STATUS_ERROR_INVALID_STATE;
    }
    currThread->state = RVCOS_THREAD_STATE_DEAD;
    currThread->ret_val = returnval;
    // if there are waiters
    // if (waiters) {
    //     tcb[waiter].returnval = rv;
    //     tcb[waiter].state = RVCOS_THREAD_STATE_READY;
    // }
    if(waiters->size != 0){
        for(int i = 0; i < waiters->size; i++){
            struct TCB* waiter = dequeue(&waiters);
            if (waiter->wait_id == thread){
                waiter->ret_val = returnval;
                waiter->state = RVCOS_THREAD_STATE_READY;
                enqueueThread(waiter);
            }
            else{
                enqueue(waiters, waiter);
            }
        }
    }
}

TStatus RVCThreadWait(TThreadID thread, TThreadReturnRef returnref) {
    struct TCB* currThread = threadArray[get_tp()]; 
    struct TCB* waitThread = threadArray[thread]; 
    if (waitThread->state != RVCOS_THREAD_STATE_DEAD) {
        currThread->state = RVCOS_THREAD_STATE_WAITING;
        //->waiter = thread;
        currThread->wait_id = thread;
        enqueue(waiters,currThread);
        schedule();
        *returnref = currThread->ret_val;
        return RVCOS_STATUS_SUCCESS;
    } else {
        *returnref = waitThread->ret_val;
    }
}

TStatus RVCThreadID(TThreadIDRef threadref){
    if (threadref == NULL){
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    threadref = get_tp();
    return RVCOS_STATUS_SUCCESS;
}

TStatus RVCThreadState(TThreadID thread, TThreadStateRef state){
    if (state == NULL){
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    else if (threadArray[thread] == NULL){
        return RVCOS_STATUS_ERROR_INVALID_ID;
    }
    state = threadArray[thread]->state;
    return RVCOS_STATUS_SUCCESS;
}

void schedule(){
    struct TCB* current = threadArray[get_tp()];
    struct TCB* nextT;
    for(int i = 0; i < 4; i++){
        nextT = dequeue(&readyQ[i]);
        if (nextT != NULL){
            continue;
        }
    }
    nextT->state = RVCOS_THREAD_STATE_RUNNING;
    if(current != nextT){
        ContextSwitch(&current->sp, nextT->sp);
    }
}

int main() {
    saved_sp = &CART_STAT_REG; // was used to see how the compiler would assign the save_sp so we could 
    while(1){                      // do it in assembly in the enter_cartridge function
        if(CART_STAT_REG & 0x1){
            enter_cartridge();
        }
    }
    return 0;
}

uint32_t c_syscall_handler(uint32_t p1,uint32_t p2,uint32_t p3,uint32_t p4,uint32_t p5,uint32_t code){
    switch(code){
        case 0x00: return RVCInitialize((void *)p1);
        case 0x01: return RVCThreadCreate((void *)p1, p2, p3, p4, p5);
        case 0x02: return RVCThreadDelete((void *)p1);
        case 0x03: return RVCThreadActivate((void *)p1);
        // case 0x04: return RVCThreadTerminate((void *)p1, p2);
        // case 0x05: return RVCThreadWait((void *)p1, p2);
        // case 0x06: return RVCThreadID((void *)p1);
        // case 0x07: return RVCThreadState((void *)p1, p2);
        // case 0x08: return RVCThreadSleep((void *)p1);
        // case 0x09: return RVCTickMS((void *)p1);
        // case 0x0A: return RVCTickCount((void *)p1);
        case 0x0B: return RVCWriteText((void *)p1, p2);
        case 0x0C: return RVCReadController((void *)p1);
    }
    return code + 1;
}
