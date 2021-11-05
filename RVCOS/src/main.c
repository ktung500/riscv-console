#include <stdint.h>
#include <stdlib.h>
#include "RVCOS.h"

volatile int global;
volatile int cursor; // used for global cursor
volatile int tick_count;
volatile uint32_t controller_status = 0;
volatile uint32_t *saved_sp;
uint32_t app_global_p;
#define MTIMECMP_LOW    (*((volatile uint32_t *)0x40000010))
#define MTIMECMP_HIGH   (*((volatile uint32_t *)0x40000014))
typedef void (*TFunctionPointer)(void);
void enter_cartridge(void);
uint32_t call_th_ent(void *param, TThreadEntry entry, uint32_t *gp);
TThreadID get_tp(void);
void set_tp(TThreadID tid);
void ContextSwitch(volatile uint32_t **oldsp, volatile uint32_t *newsp);
#define CART_STAT_REG (*(volatile uint32_t *)0x4000001C)
#define CONTROLLER_STATUS_REG (*(volatile uint32_t*)0x40000018) // base address of the Multi-Button Controller Status Register
volatile char *VIDEO_MEMORY = (volatile char *)(0x50000000 + 0xFE800);  // taken from riscv-example, main code
struct TCB* threadArray[256];
struct TCB* waiter[256];
volatile int numWatiers;
struct TCB* sleepers[256];
volatile int numSleepers;
volatile int sleeperCursor;
volatile TThreadID global_tid_nums = 2;  // should only be 2-256
volatile int num_of_threads = 0;
int highPQ[256];
int highFront = 0;
int highRear = -1;
int highSize = 0;
int norPQ[256];
int norFront = 0;
int norRear = -1;
int norSize = 0;
int lowPQ[256];
int lowFront = 0;
int lowRear = -1;
int lowSize = 0;
// for waiter queue
int waiters[256];
int waitFront = 0;
int waitRear = -1;
int waitSize = 0;

void insertWaiter(int tid){
    //RVCWriteText("ins waiter\n", 11);
     if(waitSize != 256) {
            if(waitRear == 255) {
                waitRear = -1;
            }

            waiters[++waitRear] = tid;
            waitSize++;
        }
}

int removeWaiter(){
    //RVCWriteText("rem waiter\n", 11);
    int data = -1;
    if (waitSize > 0) {
            data = waiters[waitFront++];
        }
        if(waitFront == 256) {
            waitFront = 0;
        }
        waitSize--;
    return data;
}

//void insert(int data, int* PQ, int front, int rear, int size ) {
void insert(int data, TThreadPriority priority) {
    if (priority == RVCOS_THREAD_PRIORITY_HIGH){
        if(highSize != 256) {
            if(highRear == 255) {
                highRear = -1;
            }

            highPQ[++highRear] = data;
            highSize++;
            //RVCWriteText("Ins high\n", 9);
        }
    } else if (priority == RVCOS_THREAD_PRIORITY_NORMAL){
        if(norSize != 256) {
            if(norRear == 255) {
                norRear = -1;
            }

            norPQ[++norRear] = data;
            norSize++;
            //RVCWriteText("Ins nor\n", 8);
        }
    } else if (priority == RVCOS_THREAD_PRIORITY_LOW){
        if(lowSize != 256) {
            if(lowRear == 255) {
                lowRear = -1;
            }

            lowPQ[++lowRear] = data;
            lowSize++;
            //RVCWriteText("Ins low\n", 8);
        }
    }

//    if(size != 256) {

//       if(rear == 255) {
//          rear = -1;
//       }

//       PQ[++rear] = data;
//       size++;
//    }
}

int removeData(TThreadPriority prio) {
    int data = -1;
    if (prio == RVCOS_THREAD_PRIORITY_HIGH) {
        if (highSize > 0) {
            data = highPQ[highFront++];
        }
        if(highFront == 256) {
            highFront = 0;
        }
        highSize--;
    } else if (prio == RVCOS_THREAD_PRIORITY_NORMAL) {
        data = norPQ[norFront++];
            if(norFront == 256) {
                norFront = 0;
            }
            norSize--;
    } else if (prio == RVCOS_THREAD_PRIORITY_LOW) {
        data = lowPQ[lowFront++];
            if(lowFront == 256) {
                lowFront = 0;
            }
            lowSize--;
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
    RVCWriteText("idle\n", 5);
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


    currThread->ret_val = call_th_ent(param, entry, &app_global_p);
    asm volatile ("csrci mstatus, 0x8");
    RVCThreadTerminate(thread_id, currThread->ret_val); //(TThreadReturn)
    // Disable intterupts before terminate
    //Threadterminate;

}

TStatus RVCInitialize(uint32_t *gp) {
    struct TCB* mainThread = (struct TCB*)malloc(sizeof(struct TCB)); // initializing TCB of main thread
    mainThread->tid = 0;
    mainThread->state = RVCOS_THREAD_STATE_RUNNING;
    mainThread->priority = RVCOS_THREAD_PRIORITY_NORMAL;
    //mainThread->pid = -1; // main thread has no parent so set to -1
    threadArray[0] = mainThread;
    set_tp(mainThread->tid);


    struct TCB* idleThread = (struct TCB*)malloc(sizeof(struct TCB)); // initializing TCB of idle thread
    idleThread->tid = 1;
    idleThread->state = RVCOS_THREAD_STATE_READY;   // idle thread needs to be in ready state
    idleThread->priority = RVCOS_THREAD_PRIORITY_LOWEST;
    threadArray[1] = idleThread;
    idleThread->entry = (TThreadEntry)idle;
    //uint32_t idleThreadStack[1024];
    //idleThread->stack_base = (uint8_t*)idleThreadStack;
    idleThread->stack_base = (uint8_t*)malloc(1024);
    idleThread->sp = init_Stack((uint32_t*)(idleThread->stack_base + 1024), (TThreadEntry)(idleThread->entry), (uint32_t)(idleThread->param), idleThread->tid);

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
        if (cursor < 2304) { // 2304 = 64 columns x 36 rows

            for (int i = 0; i < (int)writesize; i++) {
                char c = buffer[i];
                VIDEO_MEMORY[cursor] = ' ';
                if (c == '\n') {
                    cursor += 0x40;
                    cursor = cursor & ~0x3F;
                    VIDEO_MEMORY[cursor] = c;
                } else if(c == '\b') {
                    cursor -= 1;
                    VIDEO_MEMORY[cursor] = c;
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
            //uint32_t newThreadStack[memsize];
            //newThread->stack_base = (uint8_t*)newThreadStack; // initialize stack of memsize for the newThread
            newThread->stack_base = (uint8_t*)malloc(memsize);
            newThread->entry = entry;
            newThread->param = param;
            newThread->memsize = memsize;
            newThread->tid = currThreadID;
            *tid = newThread->tid;
            newThread->state = RVCOS_THREAD_STATE_CREATED;
            newThread->priority = prio;
            //newThread->pid = -1;
            threadArray[currThreadID] = newThread;
            num_of_threads++;
        }
        else{
            struct TCB* newThread = (struct TCB*)malloc(sizeof(struct TCB)); // initializing TCB of a thread
            // newThread->stack_base = malloc(memsize); // initialize stack of memsize for the newThread
            //uint32_t newThreadStack[memsize];
            //newThread->stack_base = (uint8_t*)newThreadStack; // initialize stack of memsize for the newThread
            uint32_t *base = malloc(memsize);
            newThread->stack_base = (uint8_t*)base;
            newThread->entry = entry;
            newThread->param = param;
            newThread->memsize = memsize;
            newThread->tid = global_tid_nums;
            *tid = global_tid_nums;
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
    struct TCB* actThread = threadArray[thread];
    if (actThread->state != RVCOS_THREAD_STATE_DEAD && actThread->state != RVCOS_THREAD_STATE_CREATED){
        return  RVCOS_STATUS_ERROR_INVALID_STATE;
    }
    else{
        //readyQ = createQueue(4);
        actThread->sp = init_Stack((uint32_t *)(actThread->stack_base + actThread->memsize), (TThreadEntry)skeleton, actThread->tid, thread);
        //currThread->sp = init_Stack((uint32_t*)(currThread->stack_base + currThread->memsize), &skeleton, currThread->tid, thread); // initializes stack/ activates thread
        actThread->state = RVCOS_THREAD_STATE_READY;
        //RVCWriteText("Activate: ", 10);
        //char buffer[1];
        //const char *Ptr = itoa(actThread -> tid, buffer, 10);
        //RVCWriteText(Ptr, 1);
        //RVCWriteText("\n", 1);
        //RVCWriteText("goes here\n",11);
        enqueueThread(actThread);
        struct TCB* currentThread = threadArray[get_tp()];
        if(actThread->priority > currentThread->priority){
            enqueueThread(currentThread);
            schedule();
        }
        //schedule();
        // call scheduler
        return RVCOS_STATUS_SUCCESS;
    }
}

void enqueueThread(struct TCB* thread){
    if(thread->priority == RVCOS_THREAD_PRIORITY_LOW){
        //RVCWriteText("insert low\n", 11);
        insert(thread->tid, RVCOS_THREAD_PRIORITY_LOW);
    }
    else if(thread->priority == RVCOS_THREAD_PRIORITY_NORMAL){
        //RVCWriteText("insert norm\n", 12);
        insert(thread->tid, RVCOS_THREAD_PRIORITY_NORMAL);
    }
    else if(thread->priority == RVCOS_THREAD_PRIORITY_HIGH) {
        //RVCWriteText("insert high\n", 12);
        insert(thread->tid, RVCOS_THREAD_PRIORITY_HIGH);
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
    // 
    currThread->state = RVCOS_THREAD_STATE_DEAD;
    RVCWriteText("set ret_val in terminate: ", 27);
    char buffer[5];
    const char *Ptr = itoa(returnval, buffer, 10);
    RVCWriteText(Ptr, 1);
    RVCWriteText("\n", 5);
    returnval = currThread->ret_val;

    if(waitSize != 0){
        int flag = 0;
        for(int i = 0; i < waitSize; i++){
            int waiterTID = removeWaiter();
            struct TCB* waiter = threadArray[waiterTID];
            if (waiter->wait_id == thread){
                //RVCWriteText("gets here1\n",11);
                //returnval = *waiter->ret_val;
                waiter->state = RVCOS_THREAD_STATE_READY;
                enqueueThread(waiter);
                if(waiter->priority > currThread->priority){
                    flag = 1;
                }
            }
            else{
                RVCWriteText("reinsert waiter\n", 16);
                insertWaiter(waiter->tid);
            }
        }
        if(flag == 1){
            //RVCWriteText("never\n", 5);
            schedule();
        }
    }
    //If the thread terminating is the current running thread, then you will definitely need to schedule.
    if(threadArray[get_tp()] == currThread){
        RVCWriteText("high terminates\n", 16);
        schedule();
    }
    return RVCOS_STATUS_SUCCESS;
}

TStatus RVCThreadWait(TThreadID thread, TThreadReturnRef returnref) {
    if (threadArray[thread] == NULL){
        return RVCOS_STATUS_ERROR_INVALID_ID;
    }
    else if(returnref == NULL){
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    struct TCB* currThread = threadArray[get_tp()];
    struct TCB* waitThread = threadArray[thread];
    if (waitThread->state != RVCOS_THREAD_STATE_DEAD) {
        //currThread->ret_val = returnref;
        currThread->state = RVCOS_THREAD_STATE_WAITING;
        //->waiter = thread;
        currThread->wait_id = thread;
        
        //enqueue(waiters,currThread);
        insertWaiter(currThread->tid);
        schedule();
        //*returnref = waitThread->ret_val;
        RVCWriteText("Wait after schedule: \n", 21);

        RVCWriteText("set ret_ref in wait\n", 20);
        *returnref = (TThreadReturn)threadArray[get_tp()]->ret_val;
        
        return RVCOS_STATUS_SUCCESS;
    // else {
    //     //waitThread->ret_val = returnref;
    //     RVCWriteText("gets here\n",10);
    //     returnref = waitThread->ret_val;
    }
    RVCWriteText("set ret_ref in wait 2\n", 22);
    *returnref = (TThreadReturn)waitThread->ret_val;
    return RVCOS_STATUS_SUCCESS;
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
    //RVCWriteText("current: ", 10);
    //char buffer[1];
    //const char *Ptr = itoa(threadArray[get_tp()] -> tid, buffer, 10);
    //RVCWriteText(Ptr, 1);
    //RVCWriteText("\n", 1);
    int nextTid = 0;
    struct TCB* nextT;
    //RVCWriteText("Test Start\n", 11);
    if (highSize != 0) {
        RVCWriteText("rem high\n", 9);
        nextTid = removeData(RVCOS_THREAD_PRIORITY_HIGH);
    } else if (norSize != 0) {
        RVCWriteText("rem norm\n", 9);
        nextTid = removeData(RVCOS_THREAD_PRIORITY_NORMAL);
    } else if (lowSize != 0) {
        RVCWriteText("rem low\n", 8);
        nextTid = removeData(RVCOS_THREAD_PRIORITY_LOW);
    } else {
        RVCWriteText("Nothing in queue\n", 17);
        nextTid = 1;
    }
    //nextTid = 2;
    nextT = threadArray[nextTid];
    //RVCWriteText("next: ", 6);
    //char buffer1[1];
    //const char *Ptr1 = itoa(nextT -> tid, buffer1, 10);
   // RVCWriteText(Ptr1, 1);
    //RVCWriteText("\n", 1);
    nextT->state = RVCOS_THREAD_STATE_RUNNING;
    if(threadArray[get_tp()]->tid != nextT->tid){
        if(current->state != RVCOS_THREAD_STATE_DEAD && current->state != RVCOS_THREAD_STATE_WAITING && nextT->state != RVCOS_THREAD_STATE_DEAD){
            current->state = RVCOS_THREAD_STATE_READY;
            RVCWriteText("Enqueue thread\n", 15);
            enqueueThread(current);
        }
        //RVCWriteText("Step 1\n", 7); // currently goes into context switch but returns to the wrong
        //ContextSwitch(&current->sp, nextT->sp);
        RVCWriteText("context switch\n", 15);
        ContextSwitch((void *)&current->sp, threadArray[nextTid]->sp);
    }
}

TStatus RVCThreadSleep(TTick tick) {
    if (tick == RVCOS_TIMEOUT_INFINITE){
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    else if(tick == RVCOS_TIMEOUT_IMMEDIATE){
        struct TCB* current = threadArray[get_tp()];
        TThreadPriority currPrio = current->priority;
        
        int next = removeData(currPrio);
        if (next == 1) {
            enqueueThread(current);
        }
        struct TCB* nextRT = threadArray[next];
        enqueueThread(nextRT);
    } else {
        struct TCB* current = threadArray[get_tp()];
        current->ticks = tick;
        current->state = RVCOS_THREAD_STATE_WAITING;
        sleepers[sleeperCursor] = current;
        sleeperCursor++;
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
        case 0x05: return RVCThreadWait((TThreadID)p1, (TThreadReturnRef)p2);
        case 0x06: return RVCThreadID((void *)p1);
        case 0x07: return RVCThreadState((TThreadID)p1, (uint32_t*)p2);
        case 0x08: return RVCThreadSleep((TThreadID)p1);
        case 0x09: return RVCTickMS((void *)p1);
        case 0x0A: return RVCTickCount((void *)p1);
        case 0x0B: return RVCWriteText((void *)p1, p2);
        case 0x0C: return RVCReadController((void *)p1);
    }
    return code + 1;
}
