#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "RVCOS.h"


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
volatile TMemoryPoolID global_mpid_nums = 1; // system memory pool is 0
volatile int num_mutex = 0;
struct TCB** threadArray;
volatile int num_of_threads = 0;
volatile int num_of_buffers = 0;
int threadArraySize = 256; // If it fills up, double the size
int offscreenBufferArraySize = 1000; 
struct MPCB** memPoolArray;
TStatus RVCWriteText1(const TTextCharacter *buffer, TMemorySize writesize);
struct GCB** offscreenBufferArray;
struct PCB** paletteArray;
volatile TPaletteID global_pid_nums = 0;
volatile TGraphicID global_gid_nums = 0;
volatile int num_backgrounds = 0;   // 0-3
volatile int num_large_sprites = 0; // 0-63
volatile int num_small_sprites = 0; //0-127
// Video Graphic Start -----------------------------------------------------------------------------------------------------------------------------

struct GCB{
    TGraphicID gid;
    TGraphicType type;
    TGraphicState state;
    int height;
    int width;
    TPaletteIndex *buffer;
    //uint8_t* buffer;
};

struct PCB{
    TPaletteID pid;
    void *buffer;
};
// all structs and globals taken from discussion-11-19
// struct for color
/*typedef struct{
    uint32_t DBlue : 8;
    uint32_t DGreen : 8;
    uint32_t DRed : 8;
    uint32_t DAlpha : 8;
} SColor, *SColorRef;*/   // already defined in RVCOS.h

// struct for the controls of a large sprite
typedef struct {
    uint32_t DPalette : 2;
    uint32_t DXOffset : 10;  // where it is on the screen
    uint32_t DYOffset : 9;
    uint32_t DWidth : 5;     // how big the srite is
    uint32_t DHeight : 5;
    uint32_t DReserved : 1;
} SLargeSpriteControl, *SLargeSpriteControlRef;

// struct for the controls of a small sprite
typedef struct {
    uint32_t DPalette : 2;
    uint32_t DXOffset : 10;
    uint32_t DYOffset : 9;
    uint32_t DWidth : 4;
    uint32_t DHeight : 4;
    uint32_t DZ : 3;
} SSmallSpriteControl, * SSmallSpriteControlRef;

// struct for the controls of a background
typedef struct {
    uint32_t DPalette : 2;
    uint32_t DXOffset : 10;
    uint32_t DYOffset : 10;
    uint32_t DZ : 3;
    uint32_t DReserved : 7;
} SBackgroundControl, *SBackgroundControlRef;

// struct to deal with the video controller
typedef struct {
    uint32_t DMode : 1;
    uint32_t DRefresh : 7;
    uint32_t DReserved : 24;
} SVideoControllerMode, *SVideoControllerModeRef;

// all from the discussion-11-19 code
volatile uint8_t *BackgroundData[5];  
volatile uint8_t *LargeSpriteData[64];
volatile uint8_t *SmallSpriteData[128];

// all from the discussion-11-19 code
volatile SColor *BackgroundPalettes[4];
volatile SColor *SpritePalettes[4];
volatile SBackgroundControl *BackgroundControls = (volatile SBackgroundControl *)0x500FF100;
volatile SLargeSpriteControl *LargeSpriteControls = (volatile SLargeSpriteControl *)0x500FF114;
volatile SSmallSpriteControl *SmallSpriteControls = (volatile SSmallSpriteControl *)0x500FF214;
volatile SVideoControllerMode *ModeControl = (volatile SVideoControllerMode *)0x500FF414;
extern SColor RVCOPaletteDefaultColors[];
void InitPointers(void);

// initiallizing the pointers for background palettes, sprite palettes, background data, large sprite data, and small sprite data
void InitPointers(void){
    for(int Index = 0; Index < 4; Index++){
        BackgroundPalettes[Index] = (volatile SColor *)(0x500FC000 + 256 * sizeof(SColor) * Index);
        SpritePalettes[Index] = (volatile SColor *)(0x500FD000 + 256 * sizeof(SColor) * Index);
    }
    for(int Index = 0; Index < 5; Index++){
        BackgroundData[Index] = (volatile uint8_t *)(0x50000000 + 512 * 288 * Index);
    }
    for(int Index = 0; Index < 64; Index++){
        LargeSpriteData[Index] = (volatile uint8_t *)(0x500B4000 + 64 * 64 * Index);
    }
    for(int Index = 0; Index < 128; Index++){
        SmallSpriteData[Index] = (volatile uint8_t *)(0x500F4000 + 16 * 16 * Index);
    }
}

// Video Graphic End ---------------------------------------------------------------------------------------------------------------------------------------

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

// typedef struct FSSNode_TAG FSSNode, *FSSNodeRef;

// struct FSSNode_TAG{
//     struct FSSNode_TAG *next;
// };

typedef struct FreeChunk_TAG FreeChunk, *FreeChunkRef;

struct FreeChunk_TAG{
    uint32_t size;
    uint8_t *base;
    struct FreeChunk_TAG *next;
}; 

typedef struct{
    int count;
    int structureSize;
    FreeChunkRef firstFree;
} FSSAllocator, *FSSAllocatorRef;


FSSAllocator FreeChunkAllocator;
FSSAllocator MPCBAllocator;
FSSAllocator TCBAllocator;
FSSAllocator MxAllocator;
FSSAllocator GCBAllocator;
FSSAllocator PCBAllocator;
int SuspendAllocationOfFreeChunks = 0;
FreeChunk InitialFreeChunks[8];

FreeChunkRef AllocateFreeChunk(void);
void DeallocateFreeChunk(FreeChunkRef chunk);

//void *MemoryAlloc(int size);

void FSSAllocatorInit(FSSAllocatorRef alloc, int size);
void *FSSAllocate(FSSAllocatorRef alloc);
void FSSDeallocate(FSSAllocatorRef alloc, void *obj);

// typedef struct FreeNode_TAG FreeNode, *FreeNodeRef;

// struct FreeNode_TAG{
//     struct FreeNode_TAG *next;
//     //struct FreeNode_TAG *prev;
//     uint8_t *base;
//     uint32_t size;
// };

struct MPCB{
    TMemoryPoolID mpid;
    uint8_t base;
    uint32_t size;
    uint32_t freeSize;
    FreeChunkRef firstFree;
    FreeChunkRef allocList;
};

// void *MemoryAlloc(int size){
//     AllocateFreeChunk();
//     return malloc(size);
// }

void FSSAllocatorInit(FSSAllocatorRef alloc, int size){
    alloc->count = 0;
    alloc->structureSize = size;
    alloc->firstFree = NULL;
}

void *FSSAllocate(FSSAllocatorRef alloc){
    if(!alloc->count){ // If allocator count is 0 or NULL
        RVCMemoryAllocate(alloc->structureSize * MIN_ALLOCATION_COUNT, &alloc->firstFree); // set first free of allocator
        FreeChunkRef Current = alloc->firstFree;
        for(int Index = 0; Index < MIN_ALLOCATION_COUNT; Index++){
            if(Index + 1 < MIN_ALLOCATION_COUNT){
                Current->next = (FreeChunkRef)((uint8_t *)Current + alloc->structureSize);
                Current = Current->next;
            }
            else{
                Current->next = NULL;
            }
        }
        alloc->count = MIN_ALLOCATION_COUNT;
    }
    FreeChunkRef NewStruct = alloc->firstFree;
    alloc->firstFree = alloc->firstFree->next;
    alloc->count--;
    return NewStruct;
}

void FSSDeallocate(FSSAllocatorRef alloc, void *obj){
    FreeChunkRef OldStruct = (FreeChunkRef)obj;
    alloc->count++;
    OldStruct->next = alloc->firstFree;
    alloc->firstFree = OldStruct;
}

FreeChunkRef AllocateFreeChunk(void){
    if(3 > FreeChunkAllocator.count && !SuspendAllocationOfFreeChunks){
        SuspendAllocationOfFreeChunks = 1;
        uint8_t *Ptr;
        RVCMemoryAllocate(FreeChunkAllocator.structureSize * MIN_ALLOCATION_COUNT, &Ptr);
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

struct MPCB* AllocateMPCB() {
    return FSSAllocate(&MPCBAllocator);
}

void DeallocateMPCB(struct MPCB *mpcb){
    FSSDeallocate(&MPCBAllocator,(void *)mpcb);
}

struct TCB* AllocateTCB() {
    return FSSAllocate(&TCBAllocator);
}

void DeallocateTCB(struct TCB *tcb){
    FSSDeallocate(&TCBAllocator,(void *)tcb);
}

struct Mutex* AllocateMx() {
    return FSSAllocate(&MxAllocator);
}

void DeallocateMx(struct Mutex* mx){
    FSSDeallocate(&MxAllocator,(void *)mx);
}

struct GCB* AllocateGCB() {
    return FSSAllocate(&GCBAllocator);
}

void DeallocateGCB(struct GCB* gcb){
    FSSDeallocate(&GCBAllocator,(void *)gcb);
}

struct PCB* AllocatePCB() {
    return FSSAllocate(&PCBAllocator);
}

void DeallocatePCB(struct PCB* pcb){
    FSSDeallocate(&PCBAllocator,(void *)pcb);
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
struct ReadyQ *backgroundQ;
struct ReadyQ *largeSpriteQ;
struct ReadyQ *smallSpriteQ;

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
     if(Q->size != 256) {
            if(Q->rear == 255) {
                Q->rear = -1;
            }

            Q->queue[++Q->rear] = tid;
            Q->size++;
        }
}

int removeRQ(struct ReadyQ *Q){
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
        }
    } else if (priority == RVCOS_THREAD_PRIORITY_NORMAL){
        if(Q->norSize != 256) {
            if(Q->norRear == 255) {
                Q->norRear = -1;
            }

            Q->norPQ[++(Q->norRear)] = data;
            Q->norSize++;
        }
    } else if (priority == RVCOS_THREAD_PRIORITY_LOW){
        if(Q->lowSize != 256) {
            if(Q->lowRear == 255) {
                Q->lowRear = -1;
            }

            Q->lowPQ[++(Q->lowRear)] = data;
            Q->lowSize++;
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
    FSSAllocatorInit(&FreeChunkAllocator, sizeof(FreeChunk));
    // struct MPCB* systemPool;
    // //RVCMemoryPoolAllocate(0, sizeof(struct MPCB), (void**)&systemPool);
    // systemPool->size = 10000;
    // memPoolArray[0] = systemPool;
    for(int Index = 0; Index < 8; Index++){
        DeallocateFreeChunk(&InitialFreeChunks[Index]);
    }
    RVCMemoryPoolAllocate(0, threadArraySize * sizeof(void *), (void**)&threadArray);
    for (int i = 0; i < threadArraySize; i++) {
        threadArray[i] = NULL;
    }
    scheduleQ = createQueue(256); // grow if we hit limit
    waiterQ = createReadyQ(256);
    sleeperQ = createReadyQ(256);
    writerQ = createReadyQ(256);
    backgroundQ = createReadyQ(256);
    largeSpriteQ = createReadyQ(256);
    smallSpriteQ = createReadyQ(256);
    RVCMemoryPoolAllocate(0, 256 * sizeof(void*), (void**)&mutexArray);
    for (int i = 0; i < 256; i++) {
        mutexArray[i] = NULL;
    }
    //struct MPCB*
    RVCMemoryPoolAllocate(0, 256 * sizeof(void*), (void**)&memPoolArray);
    for (int i = 1; i < 256; i++) {
        memPoolArray[i] = NULL;
    }
    RVCMemoryPoolAllocate(0, offscreenBufferArraySize * sizeof(void*), (void**)&offscreenBufferArray);
    for (int i = 0; i < offscreenBufferArraySize; i++) {
        offscreenBufferArray[i] = NULL;
    }
    RVCMemoryPoolAllocate(0, 256 * sizeof(void*), (void**)&paletteArray);
    for (int i = 0; i < 256; i++) {
        paletteArray[i] = NULL;
    }
    struct TCB* mainThread;  // initializing TCB of main thread
    RVCMemoryPoolAllocate(0, sizeof(struct TCB), (void**)&mainThread);
    FSSAllocatorInit(&MPCBAllocator, sizeof(struct MPCB));
    FSSAllocatorInit(&TCBAllocator, sizeof(struct TCB));
    FSSAllocatorInit(&MxAllocator, sizeof(struct Mutex));
    
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

    RVCMemoryPoolAllocate(0, 256, (void**)&idle_sb);
    idleThread->stack_base = idle_sb;
    idleThread->sp = init_Stack((uint32_t*)(idleThread->stack_base + 256), (TThreadEntry)(idleThread->entry), (uint32_t)(idleThread->param), idleThread->tid);
    
    // for video memory
    InitPointers(); 
    memcpy((void *)BackgroundPalettes[0],RVCOPaletteDefaultColors,256 * sizeof(SColor)); // loads the colors from DefaultPalette.c in Background Palette 0 
    memcpy((void *)SpritePalettes[0],RVCOPaletteDefaultColors,256 * sizeof(SColor)); // load the colors from DefaultPalette.c in Sprite Palette 0


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
    //if (buffer == NULL){
    //   return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    //}
    //else{
        //write out writesize characters to the location specified by buffer
        if (cursor < 2304) { // 2304 = 64 columns x 36 rows

            for (int i = 0; i < (int)writesize; i++) {
                char c = buffer[i];
                if (c == '\x1B') {
                    i++;
                    if (i > (int)writesize) {
                        continue;
                    }
                    char c = buffer[i];
                    if (c == '[') {
                        i++;
                        if (i > (int)writesize) {
                            continue;
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
                            //if (cursor % 0x40 != 0) { // only move left if not at left of screen
                                cursor -= 1;
                        } else if (c == 'H') {
                            cursor = 0;
                        } else if (isdigit(c)){
                            int skip = 0;
                            int ln = (int)c - '0';
                            if (c == '2') {
                                skip = 1;
                                i++;
                                c = buffer[i];
                                if (c == 'J') {
                                    // Erase screen (zero out video_memory)
                                    memset(VIDEO_MEMORY, 0, 2304);
                                    continue;
                                } else {
                                    //VIDEO_MEMORY[50] = 'A';
                                }
                            }
                            //VIDEO_MEMORY[52] = '4';
                            if (!skip) {
                                i++;
                                c = buffer[i];
                            }
                            // 2 digit line number
                            if (isdigit(c)) {
                                //VIDEO_MEMORY[52] = c;
                                ln = ln * 10 + (int)c - '0';
                                i++;
                                if (i > (int)writesize) {
                                    break;
                                }
                                c = buffer[i];
                            }
                            // problem if H or ln is more than 1 char
                            if (c == ';') {
                                //VIDEO_MEMORY[54] = 'B';
                                i++;
                                if (i > (int)writesize) {
                                    continue;
                                }
                                c = buffer[i];
                                int col = (int)c - '0';
                                i++;
                                if (i > (int)writesize) {
                                    continue;
                                }
                                c = buffer[i];
                                // 2 digit column number
                                if (isdigit(c)) {
                                    col = col * 10 + (int)c - '0';
                                    i++;
                                    c = buffer[i];
                                }
                                if (c == 'H') {
                                    cursor = (64 * ln) + col;
                                }
                            }
                        }
                    }
                }
                else if (c == '\n') {
                    cursor += 0x40;
                    cursor = cursor & ~0x3F;
                    if (cursor >= 2304) {
                        memmove(VIDEO_MEMORY, VIDEO_MEMORY + 0x40, 64*35);
                        memset(VIDEO_MEMORY + 64*35, 0, 64);
                        cursor -= 0x40;
                    }
                } else if(c == '\b') {
                    cursor -= 1;
                } else {
                    VIDEO_MEMORY[cursor] = c;
                    cursor++;
                }
            }
        }
        // if there are errors try casting (int)
        return RVCOS_STATUS_SUCCESS;
    }
//}

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
    //RVCWriteText1("set to dead\n", 12);
    currThread->state = RVCOS_THREAD_STATE_DEAD;
    currThread->ret_val = returnval;
    for (int i = 0; i < num_mutex; i++) {
        if (mutexArray[i]->holder == thread) {
            RVCWriteText1("releasing mx in term\n", 21);
            mutexArray[i] -> holder = NULL;
            mutexArray[i] -> unlocked = 1;
        }
    }
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
        nextTid = removeData(scheduleQ, RVCOS_THREAD_PRIORITY_LOW);
    } else {
        nextTid = 1;
    }
    nextT = threadArray[nextTid];
    if(current->tid != nextTid){
        nextT->state = RVCOS_THREAD_STATE_RUNNING;
        
        ContextSwitch((void *)&current->sp, threadArray[nextTid]->sp);
        
    }
    else{
        current->state = RVCOS_THREAD_STATE_RUNNING;
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
        return RVCOS_STATUS_SUCCESS;
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

TStatus RVCMemoryPoolCreate(void  *base,  TMemorySize  size,  TMemoryPoolIDRef memoryref) {
    RVCWriteText1("mpool create\n", 13);
    if (base == NULL || size < 128) {
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    if (memoryref == NULL) {
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    struct MPCB *memPool;
    memPool = AllocateMPCB();
    memPool->base = base;
    memPool->size = size;
    memPool->freeSize = size;

    memPool->firstFree = AllocateFreeChunk();
    memPool->firstFree->size = size;
    memPool->firstFree->base = base;
    memPool->firstFree->next = NULL;
    memPool->allocList = NULL;
    memPool->mpid = global_mpid_nums;
    memPoolArray[global_mpid_nums] = memPool;
    *memoryref = global_mpid_nums;
    global_mpid_nums++;
    return RVCOS_STATUS_SUCCESS;
}

TStatus RVCMemoryPoolDelete(TMemoryPoolID memory) {
    RVCWriteText1("mpool delete\n", 13);
    if (memory == RVCOS_MEMORY_POOL_ID_SYSTEM) { // Or if memory is invalid memory pool
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    if (memPoolArray[memory] == NULL || memory == -1){
        return RVCOS_STATUS_ERROR_INVALID_ID;
    }
    struct MPCB* curPool = memPoolArray[memory];
    if (curPool->allocList != NULL) {
        return RVCOS_STATUS_ERROR_INVALID_STATE;
    }
    else{
        global_mpid_nums--;
        RVCMemoryPoolDeallocate(0, curPool);
        curPool = NULL;
        memPoolArray[memory] = NULL;
        return RVCOS_STATUS_SUCCESS;
    }
    // if any memory has been allocated from the pool and is not deallocated
    //   return RVCOS_STATUS_ERROR_INVALID_STATE 
}

TStatus RVCMemoryPoolQuery(TMemoryPoolID memory, TMemorySizeRef bytesleft) {
    if (bytesleft == NULL) { // Or if memory is invalid memory pool
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    else if (memory == 0) {
        //*bytesleft = memPoolArray[0]->size;
        *bytesleft = 1000000;
        return RVCOS_STATUS_SUCCESS;
    }
    else if (memory == -1 || memPoolArray[memory] == NULL) { // if memory is invalid memory pool
        return RVCOS_STATUS_ERROR_INVALID_ID;
    } else {
        *bytesleft = memPoolArray[memory]->freeSize;
        return RVCOS_STATUS_SUCCESS;
    }
}

// allocsc
// check for looping lists
TStatus RVCMemoryPoolAllocate(TMemoryPoolID memory, TMemorySize size, void **pointer) {
    if (size == 0 || pointer == NULL ) { // Or if memory is invalid memory pool
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    else if (memory == RVCOS_MEMORY_POOL_ID_SYSTEM){
        if (size > 1000000) {
            RVCWriteText1("insuff resrc mempoolalloc\n", 26);
            return RVCOS_STATUS_ERROR_INSUFFICIENT_RESOURCES;
        } else {
            *pointer = (void *)malloc(size);
            return RVCOS_STATUS_SUCCESS;
        }
        
    }
    else if (memPoolArray[memory] == NULL || memory == -1){
        RVCWriteText1("null resrc mempoolalloc\n", 24);
        return RVCOS_STATUS_ERROR_INVALID_ID;
    }
    else if(memPoolArray[memory]->freeSize < size){
        RVCWriteText1("non-sys insuff resrc mempoolalloc\n", 33);
        return RVCOS_STATUS_ERROR_INSUFFICIENT_RESOURCES;
    }
    else{
        //RVCWriteText1("mpool alloc\n", 12);
        struct MPCB *currPool = memPoolArray[memory];
        uint32_t alloc_size = ((size + 63)/64) * 64;
        FreeChunkRef cur = currPool->firstFree;
        FreeChunkRef prev = NULL;
        while(cur) {
            if (alloc_size <= cur->size) {
                
                if (alloc_size == cur->size) { // allocate entire size
                    if (prev) {
                        prev->next = cur->next;
                    } else {
                        currPool->firstFree = cur->next;
                    }
                    FreeChunkRef tmp = currPool->allocList; // add newnode to alloc
                    cur -> next = tmp;                      // move cur from free to alloc list
                    currPool -> allocList = cur;             // alloc list
                    *pointer = cur->base;               // return ptr cur->base
                    currPool->freeSize -= alloc_size;
                    currPool->firstFree == NULL;
                    return RVCOS_STATUS_SUCCESS;

                }
                else {
                    FreeChunk *newnode = AllocateFreeChunk();
                    newnode->base = cur->base;
                    newnode->size = alloc_size;
                    cur->base += alloc_size;
                    //curPool->size -= alloc_size;

                    FreeChunkRef tmp = currPool->allocList; // add newnode to alloc
                    newnode -> next = tmp;
                    currPool -> allocList = newnode;
                    currPool->freeSize -= alloc_size;
                    *pointer = newnode->base; // return newnode->base
                    return RVCOS_STATUS_SUCCESS;
                }
            }
            prev = cur;
            cur = cur->next;
        }

        return RVCOS_STATUS_ERROR_INSUFFICIENT_RESOURCES;
    }
}

TStatus RVCMemoryPoolDeallocate(TMemoryPoolID memory, void *pointer) {
    //RVCWriteText1("mpool dealloc\n", 14);
    if (pointer == NULL) { // Or if memory is invalid memory pool
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    if (memory == 0) {
        free(pointer);
        return RVCOS_STATUS_SUCCESS;
    }
    if (memory == -1 || memPoolArray[memory] == NULL) {
        return RVCOS_STATUS_ERROR_INVALID_ID;
    }
    struct MPCB* mPool = memPoolArray[memory];
    FreeChunkRef curAlloc = mPool->allocList;
    FreeChunkRef prevAlloc = NULL; 
    FreeChunkRef curFree = mPool->firstFree;
    FreeChunkRef prevFree = NULL; 
    FreeChunkRef newFree = NULL; 
    while (curAlloc) {
        if (curAlloc->base == pointer) {
            // found in alloclist
            if (prevAlloc) { // If prevAlloc exists (its not first one chunk in freelist)
                if (curAlloc->next) {
                   prevAlloc->next = curAlloc->next;
                } else {
                   prevAlloc->next = NULL;
                }
                //prevAlloc->next = curAlloc->next;
                
            }
            else { // the chunk found is first in freelist
                if (curAlloc->next) {
                   mPool->allocList = curAlloc->next;
                } else {
                   mPool->allocList = NULL;
                }
                //mPool->allocList = curAlloc->next;
                
            }
             // remove node from alloclist
            newFree = curAlloc;
            break;
        }
        prevAlloc = curAlloc;
        curAlloc = curAlloc->next;
    }
    if (!curAlloc) {
        RVCWriteText1("null curalloc\n", 14);
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }

    while(curFree) { //trying to add newFree back to freelist
        if (newFree->base < curFree->base) { // if the deallocating chunk base is to left of freelist chunk base
            newFree->next = curFree;    // deallocating chunk's next is freelist chunk
            if (prevFree) {
                prevFree->next = newFree;
            } else {
                mPool->firstFree = newFree;
            }
            break;
        }
        prevFree = curFree;
        curFree = curFree->next;
    }
    if (!curFree) {
        if (prevFree) {
            prevFree->next = newFree; // (list is full except for the back) insert at end of list
        } else {
            mPool->firstFree = newFree; // (list is empty) insert as first
            newFree->next = NULL;
        }
        // didn't insert newfree into freelist
        // either freelist empty or ran through entire list
    }
    if (prevFree && prevFree->base + prevFree->size == newFree->base) {
        prevFree->size += newFree->size;
        prevFree->next = newFree->next;
        DeallocateFreeChunk(newFree);
        newFree = prevFree;
    }
    if (curFree && newFree->base + newFree->size == curFree->base) {
        newFree->size += curFree->size;
        newFree->next = curFree->next;
        DeallocateFreeChunk(curFree);
    }
    mPool->freeSize += curFree->size;

    return RVCOS_STATUS_SUCCESS;
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
    return RVCOS_STATUS_SUCCESS;

}

TStatus RVCMutexDelete(TMutexID mutex) {
    if(mutexArray[mutex] == NULL){
        return RVCOS_STATUS_ERROR_INVALID_ID;
    }
    else if(mutexArray[mutex]->unlocked == 0){
        RVCWriteText1("locked in delete\n", 17);
        return RVCOS_STATUS_ERROR_INVALID_STATE;
    }
    else{
        struct Mutex *mx = mutexArray[mutex];
        RVCMemoryPoolDeallocate(0, mx);
        mx = NULL;
        mutexArray[mutex] = NULL;
        //num_mutex--;
        return RVCOS_STATUS_SUCCESS;   
    }
}

TStatus RVCMutexQuery(TMutexID mutex, TThreadIDRef ownerref) {
    if (ownerref == NULL) {
        RVCWriteText1("null owner\n", 11);
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER; 
    }
    // If mutex is unlocked
    // If mutex doesn't exist
    if (mutexArray[mutex] == NULL || mutex == -1) {
        RVCWriteText1("noId ownver\n", 12);
        return RVCOS_STATUS_ERROR_INVALID_ID;
    }
    if (mutexArray[mutex]->unlocked) {
        RVCWriteText1("unlock owner\n", 14);
        *ownerref = RVCOS_THREAD_ID_INVALID;
        return RVCOS_STATUS_SUCCESS;
    }
    *ownerref = mutexArray[mutex] -> holder;
    return RVCOS_STATUS_SUCCESS;
}

TStatus RVCMutexAcquire(TMutexID mutex, TTick timeout) {
    if (mutexArray[mutex] == NULL) {
        return RVCOS_STATUS_ERROR_INVALID_ID;
    }
    struct Mutex *mx = mutexArray[mutex];
    struct TCB *currThread = threadArray[get_tp()];
    // If timeout specified as IMMEDIATE, current returns immediately if mutex is locked 
    if (timeout == RVCOS_TIMEOUT_IMMEDIATE) {
        if (mx->unlocked != 1) {
            return RVCOS_STATUS_FAILURE;
        } else {
            return RVCOS_STATUS_SUCCESS;
        }
    }
    // If timeout is specified as INFINITE, thread will block until mutex is acquired. 
    else if (timeout == RVCOS_TIMEOUT_INFINITE) {
        // check if mutex is unlocked
        if (mx->unlocked == 1){
            // mutex is now held by the current running thread
            mx->holder = currThread->tid;
            // lock mutex
            mx->unlocked = 0;
        }else{
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
    if (mutexArray[mutex] == NULL) {
        //RVCWriteText1("invalid release id\n", 19);
        return RVCOS_STATUS_ERROR_INVALID_ID;
    }
    else if(mutexArray[mutex]->holder != get_tp()){ 
        //RVCWriteText1("holder not current\n", 19);
        return RVCOS_STATUS_ERROR_INVALID_STATE;
    }
    else{
        //RVCWriteText1("releasing mx\n", 13);
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
                threadArray[get_tp()]->state = RVCOS_THREAD_STATE_READY;
                enqueueThread(threadArray[get_tp()]);
                schedule();
            }
            
            return RVCOS_STATUS_SUCCESS;
        }
    }
}

TStatus RVCChangeVideoMode(TVideoMode mode){
    if (mode != RVCOS_VIDEO_MODE_TEXT && mode != RVCOS_VIDEO_MODE_GRAPHICS){
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    ModeControl->DMode = mode; // changes the mode, and then blocks til next video refresh
    struct TCB* currentThread = threadArray[get_tp()];
    currentThread->buffer = NULL;
    currentThread->writesize = 0; // this will signal that its a video mode change instead of blocking for a write
    currentThread->state = RVCOS_THREAD_STATE_WAITING;
    insertRQ(writerQ, currentThread->tid);
    // Insert current before scheduling
    schedule();
    return RVCOS_STATUS_SUCCESS;
}

TStatus RVCGraphicCreate(TGraphicType type, TGraphicIDRef gidref){
    if(type != RVCOS_GRAPHIC_TYPE_FULL && type != RVCOS_GRAPHIC_TYPE_LARGE && type != RVCOS_GRAPHIC_TYPE_SMALL){
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    else if(gidref == NULL){
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    if(num_of_buffers >= offscreenBufferArraySize){  // number of buffers exceeeds current size of array
            // double size
            offscreenBufferArraySize *= 2;
            int newSize = offscreenBufferArraySize;
            struct GCB **temp;
            RVCMemoryPoolAllocate(0, offscreenBufferArraySize * sizeof(void *), (void**)temp);
            for (int i=0; i < offscreenBufferArraySize; i++) {
                temp[i] = offscreenBufferArray[i];
            }
            for (int i = 0; i < offscreenBufferArraySize; i++) {
                offscreenBufferArray[i] = NULL;
            }
            RVCMemoryDeallocate(offscreenBufferArray);
            offscreenBufferArray = temp;
            offscreenBufferArraySize = newSize;
    }
    struct GCB* newGraphic = AllocateGCB();
    if(type == RVCOS_GRAPHIC_TYPE_FULL){
        RVCMemoryAllocate(512*288,&newGraphic->buffer);
        newGraphic->height = 512;
        newGraphic->width = 288;
    }
    else if(type == RVCOS_GRAPHIC_TYPE_LARGE){
        RVCMemoryAllocate(64*64,&newGraphic->buffer);
        newGraphic->height = 64;
        newGraphic->width = 64;
    }
    else{
        RVCMemoryAllocate(16*16,&newGraphic->buffer);
        newGraphic->height = 16;
        newGraphic->width = 16;
    }
    newGraphic->type = type;
    newGraphic->gid = global_gid_nums;
    newGraphic->state = RVCOS_GRAPHIC_STATE_DEACTIVATED;  
    offscreenBufferArray[global_gid_nums] = newGraphic;
    *gidref = global_gid_nums;
    global_gid_nums++;
    num_of_buffers++;
    return RVCOS_STATUS_SUCCESS;
}

TStatus RVCGraphicDelete(TGraphicID gid){
    return RVCOS_STATUS_SUCCESS;
}

void convertPosAndDim(struct GCB* graphic,SGraphicPositionRef pos, SGraphicDimensionsRef dim, TPaletteID pid, int i){
    if(graphic->type == RVCOS_GRAPHIC_TYPE_FULL){
        BackgroundControls[i].DXOffset = pos->DXPosition - 512;
        BackgroundControls[i].DYOffset = pos->DYPosition - 288;
        BackgroundControls[i].DZ = pos->DZPosition;
        BackgroundControls[i].DPalette = pid;
    }
    else if(graphic->type == RVCOS_GRAPHIC_TYPE_LARGE){
        LargeSpriteControls[i].DHeight = dim->DHeight;
        LargeSpriteControls[i].DWidth = dim->DWidth;
        LargeSpriteControls[i].DXOffset = pos->DXPosition - 64;
        LargeSpriteControls[i].DYOffset = pos->DYPosition - 64;
        LargeSpriteControls[i].DPalette = pid;
    }
    else{
        SmallSpriteControls[i].DHeight = dim->DHeight;
        SmallSpriteControls[i].DWidth = dim->DWidth;
        SmallSpriteControls[i].DXOffset = pos->DXPosition - 16;
        SmallSpriteControls[i].DYOffset = pos->DYPosition - 16;
        SmallSpriteControls[i].DPalette = pid;
    }

}

int validateParams(struct GCB* graphic,SGraphicPositionRef pos, SGraphicDimensionsRef dim, TPaletteID pid){
    int valid = 0;
    if(graphic->type == RVCOS_GRAPHIC_TYPE_FULL){
        if(pos->DXPosition < -512 || pos->DXPosition > 512){
            //not valid
            valid = -1;
        }
        else if(pos->DYPosition < -288 || pos->DYPosition > 288){
            valid = -1;
        }
        if(valid != -1){
            convertPosAndDim(graphic,pos,dim,pid,num_backgrounds);
        }
    }
    else if(graphic->type == RVCOS_GRAPHIC_TYPE_LARGE){
        if(pos->DXPosition < -64 || pos->DXPosition > 512){
            //not valid
            valid = -1;
        }
        else if(pos->DYPosition < -64 || pos->DYPosition > 288){
            valid = -1;
        }
        else if(dim->DHeight < 33 || dim->DHeight > 64){
            valid = -1;
        }
        else if(dim->DWidth < 33 || dim->DWidth > 64){
            valid = -1;
        }
        if(valid != -1){
            convertPosAndDim(graphic,pos,dim,pid,num_large_sprites);
        }
    }
    else{
        if(pos->DXPosition < -16 || pos->DXPosition > 512){
            //not valid
            valid = -1;
        }
        else if(pos->DYPosition < -16 || pos->DYPosition > 288){
            valid = -1;
        }
        else if(dim->DHeight < 1 || dim->DHeight > 16){
            valid = -1;
        }
        else if(dim->DWidth < 1 || dim->DWidth > 16){
            valid = -1;
        }
        if(valid != -1){
            convertPosAndDim(graphic,pos,dim,pid,num_small_sprites);
        }
    }
}

TStatus RVCGraphicActivate(TGraphicID gid, SGraphicPositionRef pos, SGraphicDimensionsRef dim, TPaletteID pid){
  /*  Upon successful activation of the background buffer, RVCGraphicActivate() returns 
    RVCOS_STATUS_SUCCESS. If the graphic buffer identifier gid does not exist or if the palette 
    identifier pid does not exist, then RVCOS_STATUS_ERROR_INVALID_ID is returned. If pos is 
    NULL, dim is NULL for a non-FULL graphic, or if pos or dim non-NULL but are out of range for 
    the  type  of  graphic  then  RVCOS_STATUS_ERROR_INVALID_PARAMETER  is  returned.  If 
    there are insufficient video hardware resources to activate the graphic buffer, then 
    RVCOS_STATUS_ERROR_INSUFFICIENT_RESOURCES  is  returned.  If  the  graphic  buffer 
    has a pending activation, then RVCOS_STATUS_ERROR_INVALID_STATE is returned*/
    if(offscreenBufferArray[gid] == NULL){ // also need to include if the palette exists
        return RVCOS_STATUS_ERROR_INVALID_ID;
    }
    struct GCB* graphic = offscreenBufferArray[gid];
    if(dim == NULL && pos == NULL && (graphic->type == RVCOS_GRAPHIC_TYPE_LARGE || graphic->type == RVCOS_GRAPHIC_TYPE_SMALL)){
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER; 
    }
    if(validateParams(graphic,pos,dim,pid) == -1){
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    if(graphic->state == RVCOS_GRAPHIC_STATE_PENDING){
        return RVCOS_STATUS_ERROR_INVALID_STATE;
    }
    graphic->state = RVCOS_GRAPHIC_STATE_PENDING;
    if(graphic->type == RVCOS_GRAPHIC_TYPE_FULL){
        insertRQ(backgroundQ, gid);
    }
    else if(graphic->type == RVCOS_GRAPHIC_TYPE_LARGE){
        insertRQ(largeSpriteQ, gid);
    }
    else{
        insertRQ(smallSpriteQ, gid);
    }
    return RVCOS_STATUS_SUCCESS;
}

TStatus RVCGraphicDeactivate(TGraphicID gid){
    if(offscreenBufferArray[gid] == NULL){
        return RVCOS_STATUS_ERROR_INVALID_ID;
    }
    else if(offscreenBufferArray[gid]->state == RVCOS_GRAPHIC_STATE_ACTIVATED || offscreenBufferArray[gid]->state == RVCOS_GRAPHIC_STATE_PENDING){
        return RVCOS_STATUS_ERROR_INVALID_STATE;
    }
    offscreenBufferArray[gid]->state = RVCOS_GRAPHIC_STATE_DEACTIVATED;
    return RVCOS_STATUS_SUCCESS;
}

/*int* determineOverlap(struct GCB* graphic, SGraphicPositionRef pos, SGraphicDimensionsRef dim, uint32_t srcwidth, int graphicSize, int **bufPosArray){
    int *overlap;
    int *buf;
    RVCMemoryPoolAllocate(0, graphicSize * sizeof(int), (void**)&overlap); //147456 = max pixels in graphic
    RVCMemoryPoolAllocate(0, graphicSize * sizeof(int), (void**)&buf);
    int topLeft = (pos->DYPosition * graphic->width) + pos->DXPosition;
    int srcPos = 0;
    int graphPos = 0;
    int row = 0; // row in source
    int col = 0; // col in source
    int i = 0; // index of overlap array
    while(1) {
        if (srcPos > (dim->DHeight-1 * srcwidth) + dim->DWidth-1) {
            break;
        }
        if (col <= dim->DWidth - 1 && row <= dim->DHeight - 1) {
            overlap[i] = srcPos;
            buf[i] = graphPos + topLeft;
            i++;
        }
        if (col == srcwidth - 1) {
            col = 0;
            row++;
            graphPos += graphic->width - srcwidth - 1;
        } else {
            col++;
        }
        srcPos++;
    }
    *bufPosArray = buf;
    RVCMemoryPoolDeallocate(0, buf);
    return overlap;
}*/

void determineOverlap(struct GCB* graphic, SGraphicPositionRef pos, SGraphicDimensionsRef dim, uint32_t srcwidth, int graphicSize, int *srcBegin, int *destBegin){
    *destBegin = (pos->DYPosition * graphic->width) + pos->DXPosition;
    if(pos->DXPosition < 0 && pos->DYPosition < 0){
        *srcBegin = -pos->DYPosition*srcwidth + -pos->DXPosition;
        //*destBegin = 0;
    }
    else{
        *srcBegin = 0;
    }
}



TStatus RVCGraphicDraw(TGraphicID gid, SGraphicPositionRef pos, SGraphicDimensionsRef dim, TPaletteIndex *src, uint32_t srcwidth){
    /*Upon successful activation of the background buffer, RVCGraphicDraw() returns 
    RVCOS_STATUS_SUCCESS. If the graphic buffer identifier gid does not exist, then 
    RVCOS_STATUS_ERROR_INVALID_ID  is  returned.  If  pos  is  NULL,  dim  is  NULL,  src  is 
    NULL or srcwidth is less than DWidth in dim, then 
    RVCOS_STATUS_ERROR_INVALID_PARAMETER is returned. If the buffer has been 
    activated, but the activation has not completed (the upcall has not been invoked), then 
    RVCOS_STATUS_ERROR_INVALID_STATE is returned. */
    /*struct GCB* graphic = offscreenBufferArray[gid];
    graphic->buffer = graphic->buffer + pos->DYPosition*dim->DWidth +pos->DXPosition;
    for(int i = 0; i< row_count; i++){
        memcpy(graphic->buffer, src, col_count);
        graphic->buffer += dim->DWidth;
        src += srcwidth;
    }*/
    if(offscreenBufferArray[gid] == NULL){
        return RVCOS_STATUS_ERROR_INVALID_ID;
    }
    else if(pos == NULL || dim == NULL || src == NULL || srcwidth < dim->DWidth){
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    // need to account for the upcall once we implement it
    struct GCB* graphic = offscreenBufferArray[gid];
    int graphicSize = dim->DWidth * dim->DHeight;
    //int *bufPosArray;
    //RVCMemoryPoolAllocate(0, graphicSize * sizeof(int), (void**)&bufPosArray);
    int srcBegin, destBegin;
    determineOverlap(graphic, pos, dim, srcwidth, graphicSize, &srcBegin, &destBegin);

    //TPaletteIndex *graphic_buffer = graphic->buffer + destBegin; 

    src = src + srcBegin;

    //TPaletteIndexRef offscreenBuffer;
    //RVCMemoryAllocate(512*288, offscreenBuffer);
    TPaletteIndex *buffer = graphic->buffer + (pos->DYPosition * graphic->width) + pos->DXPosition;// + destBegin;//offscreenBuffer;// + destBegin;
    for(int i = 0; i < dim->DHeight; i++){
        memcpy(buffer, src, dim->DWidth);
        char buff[20];
        //uint8_t id = buffer[i];
        uint8_t id = src[i];
        itoa(id, buff, 10);
        RVCWriteText1(buff, 2);
        buffer += graphic->width;
        src += srcwidth;
    }
    //graphic->buffer = offscreenBuffer;

    // does not cause an mcause 5 starts here ----------------------------------------------------------------

    //memcpy(graphic_buffer, src, 1);
    //char buff[20];
    //uint8_t id = graphic_buffer[0];
    //uint8_t id = src[1];
    //itoa(id, buff, 10);
    //RVCWriteText1(buff, 1);

    // ends here ---------------------------------------------------------------------------------------------

    // causes mcause 5

    /*for(int i = 0; i< dim->DHeight; i++){
        memcpy(graphic_buffer, src, dim->DWidth);  // memcpy not working
        char buff[20];
        uint8_t id = graphic_buffer[i];
        //uint8_t id = src[i];
        itoa(id, buff, 10);
        RVCWriteText1(buff, 10);
        graphic_buffer += graphic->width;
        src += srcwidth;
    }*/

    //causes mcause 5

    /*for(int i = 0; i< graphicSize; i++){
        int buffPos = bufPosArray[i];
        int srcPos = srcOverlap[i];
        // graphic->buffer = buffer + buffPos
        memcpy((int)graphic->buffer + buffPos, src[srcPos], 1);
    }*/

    return RVCOS_STATUS_SUCCESS;

    // // ouch ouch my brain hurts. I will come back to this later
    // for (int i = 0; i < graphicSize; i++) {
    //     int srcRow = srcOverlap[i] + 1 / (dim->DWidth) - 1;
    //     int srcCol = srcOverlap[i] % (dim->DWidth) - 1;
    //     bufPosArray[i] = topLeft + srcOverlap[i];

    // }
}

TStatus RVCPaletteCreate(TPaletteIDRef pidref){
    if (pidref == NULL){
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    //Remember that palettes may be  activated by more one than graphic, so you will need  to  maintain a reference list. 
    struct PCB* newPalette = AllocatePCB();
    //newPalette->buffer = 
    RVCMemoryAllocate(256*sizeof(SColor),&newPalette->buffer);
    newPalette->pid = global_pid_nums;
    //newPalette->state = RVCOS_GRAPHIC_STATE_DEACTIVATED;  
    paletteArray[global_pid_nums] = newPalette;
    *pidref = global_pid_nums;
    global_pid_nums++;
    return RVCOS_STATUS_SUCCESS;
}

TStatus RVCPaletteDelete(TPaletteID pid){
    return RVCOS_STATUS_SUCCESS;
}

TStatus RVCPaletteUpdate(TPaletteID pid, SColorRef cols, TPaletteIndex offset, uint32_t count){
    if(paletteArray[pid] == NULL){
        return RVCOS_STATUS_ERROR_INVALID_ID;
    }
    else if(cols == NULL){
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    else if(offset-count < 0 || offset+count > 256){
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    // If the color palette is currently being used by a graphic with a pending activation, RVCOS_STATUS_ERROR_INVALID_STATE is returned. 
    struct PCB* palette = paletteArray[pid];
    SColorRef palette_offset = palette->buffer + offset;
    //palette->buffer += offset;
    //for(int i = 0; i < count; i++){
    memcpy(palette_offset, cols, count);
    //}
    return RVCOS_STATUS_SUCCESS;
}

TStatus RVCSetVideoUpcall(TThreadEntry upcall, void *param){

    return RVCOS_STATUS_SUCCESS;
}

int main() {
    // see piazza post 981
    // InitPointers(); // not sure if these go in main but or RVCInitialize, but these were in main in the discussion code
    // memcpy((void *)BackgroundPalettes[0],RVCOPaletteDefaultColors,256 * sizeof(SColor)); // loads the colors from DefaultPalette.c in Background Palette 0 
    // memcpy((void *)SpritePalettes[0],RVCOPaletteDefaultColors,256 * sizeof(SColor)); // load the colors from DefaultPalette.c in Sprite Palette 0
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
        case 0x17: return RVCChangeVideoMode (p1);
        //case 0x18: return RVCSetVideoUpcall (p1,p2);
        case 0x19: return RVCGraphicCreate (p1,p2);
        case 0x1A: return RVCGraphicDelete (p1);
        case 0x1B: return RVCGraphicActivate(p1,p2,p3,p4);
        case 0x1C: return RVCGraphicDeactivate(p1);
        case 0x1D: return RVCGraphicDraw(p1,p2,p3,p4,p5);
        // case 0x1E: return RVCPaletteCreate(p1);
        // case 0x1F: return RVCPaletteDelete(p1);
        // case 0x20: return RVCPaletteUpdate(p1,p2,p3,p4);
    }
    return code + 1;
}

SColor RVCOPaletteDefaultColors[] = {
{0x00, 0x00, 0x00, 0x00}, // Transparent
{0x00, 0x00, 0x80, 0xFF}, // Maroon (SYSTEM)
{0x00, 0x80, 0x00, 0xFF}, // Green (SYSTEM)
{0x00, 0x80, 0x80, 0xFF}, // Olive (SYSTEM)
{0x80, 0x00, 0x00, 0xFF}, // Navy (SYSTEM)
{0x80, 0x00, 0x80, 0xFF}, // Purple (SYSTEM)
{0x80, 0x80, 0x00, 0xFF}, // Teal (SYSTEM)
{0xC0, 0xC0, 0xC0, 0xFF}, // Silver (SYSTEM)
{0x80, 0x80, 0x80, 0xFF}, // Grey (SYSTEM)
{0x00, 0x00, 0xFF, 0xFF}, // Red (SYSTEM)
{0x00, 0xFF, 0x00, 0xFF}, // Lime (SYSTEM)
{0x00, 0xFF, 0xFF, 0xFF}, // Yellow (SYSTEM)
{0xFF, 0x00, 0x00, 0xFF}, // Blue (SYSTEM)
{0xFF, 0x00, 0xFF, 0xFF}, // Fuchsia (SYSTEM)
{0xFF, 0xFF, 0x00, 0xFF}, // Aqua (SYSTEM)
{0xFF, 0xFF, 0xFF, 0xFF}, // White (SYSTEM)
{0x00, 0x00, 0x00, 0xFF}, // Black (SYSTEM)
{0x5F, 0x00, 0x00, 0xFF}, // NavyBlue
{0x87, 0x00, 0x00, 0xFF}, // DarkBlue
{0xAF, 0x00, 0x00, 0xFF}, // Blue3
{0xD7, 0x00, 0x00, 0xFF}, // Blue3
{0xFF, 0x00, 0x00, 0xFF}, // Blue1
{0x00, 0x5F, 0x00, 0xFF}, // DarkGreen
{0x5F, 0x5F, 0x00, 0xFF}, // DeepSkyBlue4
{0x87, 0x5F, 0x00, 0xFF}, // DeepSkyBlue4
{0xAF, 0x5F, 0x00, 0xFF}, // DeepSkyBlue4
{0xD7, 0x5F, 0x00, 0xFF}, // DodgerBlue3
{0xFF, 0x5F, 0x00, 0xFF}, // DodgerBlue2
{0x00, 0x87, 0x00, 0xFF}, // Green4
{0x5F, 0x87, 0x00, 0xFF}, // SpringGreen4
{0x87, 0x87, 0x00, 0xFF}, // Turquoise4
{0xAF, 0x87, 0x00, 0xFF}, // DeepSkyBlue3
{0xD7, 0x87, 0x00, 0xFF}, // DeepSkyBlue3
{0xFF, 0x87, 0x00, 0xFF}, // DodgerBlue1
{0x00, 0xAF, 0x00, 0xFF}, // Green3
{0x5F, 0xAF, 0x00, 0xFF}, // SpringGreen3
{0x87, 0xAF, 0x00, 0xFF}, // DarkCyan
{0xAF, 0xAF, 0x00, 0xFF}, // LightSeaGreen
{0xD7, 0xAF, 0x00, 0xFF}, // DeepSkyBlue2
{0xFF, 0xAF, 0x00, 0xFF}, // DeepSkyBlue1
{0x00, 0xD7, 0x00, 0xFF}, // Green3
{0x5F, 0xD7, 0x00, 0xFF}, // SpringGreen3
{0x87, 0xD7, 0x00, 0xFF}, // SpringGreen2
{0xAF, 0xD7, 0x00, 0xFF}, // Cyan3
{0xD7, 0xD7, 0x00, 0xFF}, // DarkTurquoise
{0xFF, 0xD7, 0x00, 0xFF}, // Turquoise2
{0x00, 0xFF, 0x00, 0xFF}, // Green1
{0x5F, 0xFF, 0x00, 0xFF}, // SpringGreen2
{0x87, 0xFF, 0x00, 0xFF}, // SpringGreen1
{0xAF, 0xFF, 0x00, 0xFF}, // MediumSpringGreen
{0xD7, 0xFF, 0x00, 0xFF}, // Cyan2
{0xFF, 0xFF, 0x00, 0xFF}, // Cyan1
{0x00, 0x00, 0x5F, 0xFF}, // DarkRed
{0x5F, 0x00, 0x5F, 0xFF}, // DeepPink4
{0x87, 0x00, 0x5F, 0xFF}, // Purple4
{0xAF, 0x00, 0x5F, 0xFF}, // Purple4
{0xD7, 0x00, 0x5F, 0xFF}, // Purple3
{0xFF, 0x00, 0x5F, 0xFF}, // BlueViolet
{0x00, 0x5F, 0x5F, 0xFF}, // Orange4
{0x5F, 0x5F, 0x5F, 0xFF}, // Grey37
{0x87, 0x5F, 0x5F, 0xFF}, // MediumPurple4
{0xAF, 0x5F, 0x5F, 0xFF}, // SlateBlue3
{0xD7, 0x5F, 0x5F, 0xFF}, // SlateBlue3
{0xFF, 0x5F, 0x5F, 0xFF}, // RoyalBlue1
{0x00, 0x87, 0x5F, 0xFF}, // Chartreuse4
{0x5F, 0x87, 0x5F, 0xFF}, // DarkSeaGreen4
{0x87, 0x87, 0x5F, 0xFF}, // PaleTurquoise4
{0xAF, 0x87, 0x5F, 0xFF}, // SteelBlue
{0xD7, 0x87, 0x5F, 0xFF}, // SteelBlue3
{0xFF, 0x87, 0x5F, 0xFF}, // CornflowerBlue
{0x00, 0xAF, 0x5F, 0xFF}, // Chartreuse3
{0x5F, 0xAF, 0x5F, 0xFF}, // DarkSeaGreen4
{0x87, 0xAF, 0x5F, 0xFF}, // CadetBlue
{0xAF, 0xAF, 0x5F, 0xFF}, // CadetBlue
{0xD7, 0xAF, 0x5F, 0xFF}, // SkyBlue3
{0xFF, 0xAF, 0x5F, 0xFF}, // SteelBlue1
{0x00, 0xD7, 0x5F, 0xFF}, // Chartreuse3
{0x5F, 0xD7, 0x5F, 0xFF}, // PaleGreen3
{0x87, 0xD7, 0x5F, 0xFF}, // SeaGreen3
{0xAF, 0xD7, 0x5F, 0xFF}, // Aquamarine3
{0xD7, 0xD7, 0x5F, 0xFF}, // MediumTurquoise
{0xFF, 0xD7, 0x5F, 0xFF}, // SteelBlue1
{0x00, 0xFF, 0x5F, 0xFF}, // Chartreuse2
{0x5F, 0xFF, 0x5F, 0xFF}, // SeaGreen2
{0x87, 0xFF, 0x5F, 0xFF}, // SeaGreen1
{0xAF, 0xFF, 0x5F, 0xFF}, // SeaGreen1
{0xD7, 0xFF, 0x5F, 0xFF}, // Aquamarine1
{0xFF, 0xFF, 0x5F, 0xFF}, // DarkSlateGray2
{0x00, 0x00, 0x87, 0xFF}, // DarkRed
{0x5F, 0x00, 0x87, 0xFF}, // DeepPink4
{0x87, 0x00, 0x87, 0xFF}, // DarkMagenta
{0xAF, 0x00, 0x87, 0xFF}, // DarkMagenta
{0xD7, 0x00, 0x87, 0xFF}, // DarkViolet
{0xFF, 0x00, 0x87, 0xFF}, // Purple
{0x00, 0x5F, 0x87, 0xFF}, // Orange4
{0x5F, 0x5F, 0x87, 0xFF}, // LightPink4
{0x87, 0x5F, 0x87, 0xFF}, // Plum4
{0xAF, 0x5F, 0x87, 0xFF}, // MediumPurple3
{0xD7, 0x5F, 0x87, 0xFF}, // MediumPurple3
{0xFF, 0x5F, 0x87, 0xFF}, // SlateBlue1
{0x00, 0x87, 0x87, 0xFF}, // Yellow4
{0x5F, 0x87, 0x87, 0xFF}, // Wheat4
{0x87, 0x87, 0x87, 0xFF}, // Grey53
{0xAF, 0x87, 0x87, 0xFF}, // LightSlateGrey
{0xD7, 0x87, 0x87, 0xFF}, // MediumPurple
{0xFF, 0x87, 0x87, 0xFF}, // LightSlateBlue
{0x00, 0xAF, 0x87, 0xFF}, // Yellow4
{0x5F, 0xAF, 0x87, 0xFF}, // DarkOliveGreen3
{0x87, 0xAF, 0x87, 0xFF}, // DarkSeaGreen
{0xAF, 0xAF, 0x87, 0xFF}, // LightSkyBlue3
{0xD7, 0xAF, 0x87, 0xFF}, // LightSkyBlue3
{0xFF, 0xAF, 0x87, 0xFF}, // SkyBlue2
{0x00, 0xD7, 0x87, 0xFF}, // Chartreuse2
{0x5F, 0xD7, 0x87, 0xFF}, // DarkOliveGreen3
{0x87, 0xD7, 0x87, 0xFF}, // PaleGreen3
{0xAF, 0xD7, 0x87, 0xFF}, // DarkSeaGreen3
{0xD7, 0xD7, 0x87, 0xFF}, // DarkSlateGray3
{0xFF, 0xD7, 0x87, 0xFF}, // SkyBlue1
{0x00, 0xFF, 0x87, 0xFF}, // Chartreuse1
{0x5F, 0xFF, 0x87, 0xFF}, // LightGreen
{0x87, 0xFF, 0x87, 0xFF}, // LightGreen
{0xAF, 0xFF, 0x87, 0xFF}, // PaleGreen1
{0xD7, 0xFF, 0x87, 0xFF}, // Aquamarine1
{0xFF, 0xFF, 0x87, 0xFF}, // DarkSlateGray1
{0x00, 0x00, 0xAF, 0xFF}, // Red3
{0x5F, 0x00, 0xAF, 0xFF}, // DeepPink4
{0x87, 0x00, 0xAF, 0xFF}, // MediumVioletRed
{0xAF, 0x00, 0xAF, 0xFF}, // Magenta3
{0xD7, 0x00, 0xAF, 0xFF}, // DarkViolet
{0xFF, 0x00, 0xAF, 0xFF}, // Purple
{0x00, 0x5F, 0xAF, 0xFF}, // DarkOrange3
{0x5F, 0x5F, 0xAF, 0xFF}, // IndianRed
{0x87, 0x5F, 0xAF, 0xFF}, // HotPink3
{0xAF, 0x5F, 0xAF, 0xFF}, // MediumOrchid3
{0xD7, 0x5F, 0xAF, 0xFF}, // MediumOrchid
{0xFF, 0x5F, 0xAF, 0xFF}, // MediumPurple2
{0x00, 0x87, 0xAF, 0xFF}, // DarkGoldenrod
{0x5F, 0x87, 0xAF, 0xFF}, // LightSalmon3
{0x87, 0x87, 0xAF, 0xFF}, // RosyBrown
{0xAF, 0x87, 0xAF, 0xFF}, // Grey63
{0xD7, 0x87, 0xAF, 0xFF}, // MediumPurple2
{0xFF, 0x87, 0xAF, 0xFF}, // MediumPurple1
{0x00, 0xAF, 0xAF, 0xFF}, // Gold3
{0x5F, 0xAF, 0xAF, 0xFF}, // DarkKhaki
{0x87, 0xAF, 0xAF, 0xFF}, // NavajoWhite3
{0xAF, 0xAF, 0xAF, 0xFF}, // Grey69
{0xD7, 0xAF, 0xAF, 0xFF}, // LightSteelBlue3
{0xFF, 0xAF, 0xAF, 0xFF}, // LightSteelBlue
{0x00, 0xD7, 0xAF, 0xFF}, // Yellow3
{0x5F, 0xD7, 0xAF, 0xFF}, // DarkOliveGreen3
{0x87, 0xD7, 0xAF, 0xFF}, // DarkSeaGreen3
{0xAF, 0xD7, 0xAF, 0xFF}, // DarkSeaGreen2
{0xD7, 0xD7, 0xAF, 0xFF}, // LightCyan3
{0xFF, 0xD7, 0xAF, 0xFF}, // LightSkyBlue1
{0x00, 0xFF, 0xAF, 0xFF}, // GreenYellow
{0x5F, 0xFF, 0xAF, 0xFF}, // DarkOliveGreen2
{0x87, 0xFF, 0xAF, 0xFF}, // PaleGreen1
{0xAF, 0xFF, 0xAF, 0xFF}, // DarkSeaGreen2
{0xD7, 0xFF, 0xAF, 0xFF}, // DarkSeaGreen1
{0xFF, 0xFF, 0xAF, 0xFF}, // PaleTurquoise1
{0x00, 0x00, 0xD7, 0xFF}, // Red3
{0x5F, 0x00, 0xD7, 0xFF}, // DeepPink3
{0x87, 0x00, 0xD7, 0xFF}, // DeepPink3
{0xAF, 0x00, 0xD7, 0xFF}, // Magenta3
{0xD7, 0x00, 0xD7, 0xFF}, // Magenta3
{0xFF, 0x00, 0xD7, 0xFF}, // Magenta2
{0x00, 0x5F, 0xD7, 0xFF}, // DarkOrange3
{0x5F, 0x5F, 0xD7, 0xFF}, // IndianRed
{0x87, 0x5F, 0xD7, 0xFF}, // HotPink3
{0xAF, 0x5F, 0xD7, 0xFF}, // HotPink2
{0xD7, 0x5F, 0xD7, 0xFF}, // Orchid
{0xFF, 0x5F, 0xD7, 0xFF}, // MediumOrchid1
{0x00, 0x87, 0xD7, 0xFF}, // Orange3
{0x5F, 0x87, 0xD7, 0xFF}, // LightSalmon3
{0x87, 0x87, 0xD7, 0xFF}, // LightPink3
{0xAF, 0x87, 0xD7, 0xFF}, // Pink3
{0xD7, 0x87, 0xD7, 0xFF}, // Plum3
{0xFF, 0x87, 0xD7, 0xFF}, // Violet
{0x00, 0xAF, 0xD7, 0xFF}, // Gold3
{0x5F, 0xAF, 0xD7, 0xFF}, // LightGoldenrod3
{0x87, 0xAF, 0xD7, 0xFF}, // Tan
{0xAF, 0xAF, 0xD7, 0xFF}, // MistyRose3
{0xD7, 0xAF, 0xD7, 0xFF}, // Thistle3
{0xFF, 0xAF, 0xD7, 0xFF}, // Plum2
{0x00, 0xD7, 0xD7, 0xFF}, // Yellow3
{0x5F, 0xD7, 0xD7, 0xFF}, // Khaki3
{0x87, 0xD7, 0xD7, 0xFF}, // LightGoldenrod2
{0xAF, 0xD7, 0xD7, 0xFF}, // LightYellow3
{0xD7, 0xD7, 0xD7, 0xFF}, // Grey84
{0xFF, 0xD7, 0xD7, 0xFF}, // LightSteelBlue1
{0x00, 0xFF, 0xD7, 0xFF}, // Yellow2
{0x5F, 0xFF, 0xD7, 0xFF}, // DarkOliveGreen1
{0x87, 0xFF, 0xD7, 0xFF}, // DarkOliveGreen1
{0xAF, 0xFF, 0xD7, 0xFF}, // DarkSeaGreen1
{0xD7, 0xFF, 0xD7, 0xFF}, // Honeydew2
{0xFF, 0xFF, 0xD7, 0xFF}, // LightCyan1
{0x00, 0x00, 0xFF, 0xFF}, // Red1
{0x5F, 0x00, 0xFF, 0xFF}, // DeepPink2
{0x87, 0x00, 0xFF, 0xFF}, // DeepPink1
{0xAF, 0x00, 0xFF, 0xFF}, // DeepPink1
{0xD7, 0x00, 0xFF, 0xFF}, // Magenta2
{0xFF, 0x00, 0xFF, 0xFF}, // Magenta1
{0x00, 0x5F, 0xFF, 0xFF}, // OrangeRed1
{0x5F, 0x5F, 0xFF, 0xFF}, // IndianRed1
{0x87, 0x5F, 0xFF, 0xFF}, // IndianRed1
{0xAF, 0x5F, 0xFF, 0xFF}, // HotPink
{0xD7, 0x5F, 0xFF, 0xFF}, // HotPink
{0xFF, 0x5F, 0xFF, 0xFF}, // MediumOrchid1
{0x00, 0x87, 0xFF, 0xFF}, // DarkOrange
{0x5F, 0x87, 0xFF, 0xFF}, // Salmon1
{0x87, 0x87, 0xFF, 0xFF}, // LightCoral
{0xAF, 0x87, 0xFF, 0xFF}, // PaleVioletRed1
{0xD7, 0x87, 0xFF, 0xFF}, // Orchid2
{0xFF, 0x87, 0xFF, 0xFF}, // Orchid1
{0x00, 0xAF, 0xFF, 0xFF}, // Orange1
{0x5F, 0xAF, 0xFF, 0xFF}, // SandyBrown
{0x87, 0xAF, 0xFF, 0xFF}, // LightSalmon1
{0xAF, 0xAF, 0xFF, 0xFF}, // LightPink1
{0xD7, 0xAF, 0xFF, 0xFF}, // Pink1
{0xFF, 0xAF, 0xFF, 0xFF}, // Plum1
{0x00, 0xD7, 0xFF, 0xFF}, // Gold1
{0x5F, 0xD7, 0xFF, 0xFF}, // LightGoldenrod2
{0x87, 0xD7, 0xFF, 0xFF}, // LightGoldenrod2
{0xAF, 0xD7, 0xFF, 0xFF}, // NavajoWhite1
{0xD7, 0xD7, 0xFF, 0xFF}, // MistyRose1
{0xFF, 0xD7, 0xFF, 0xFF}, // Thistle1
{0x00, 0xFF, 0xFF, 0xFF}, // Yellow1
{0x5F, 0xFF, 0xFF, 0xFF}, // LightGoldenrod1
{0x87, 0xFF, 0xFF, 0xFF}, // Khaki1
{0xAF, 0xFF, 0xFF, 0xFF}, // Wheat1
{0xD7, 0xFF, 0xFF, 0xFF}, // Cornsilk1
{0xFF, 0xFF, 0xFF, 0xFF}, // Grey100
{0x08, 0x08, 0x08, 0xFF}, // Grey3
{0x12, 0x12, 0x12, 0xFF}, // Grey7
{0x1C, 0x1C, 0x1C, 0xFF}, // Grey11
{0x26, 0x26, 0x26, 0xFF}, // Grey15
{0x30, 0x30, 0x30, 0xFF}, // Grey19
{0x3A, 0x3A, 0x3A, 0xFF}, // Grey23
{0x44, 0x44, 0x44, 0xFF}, // Grey27
{0x4E, 0x4E, 0x4E, 0xFF}, // Grey30
{0x58, 0x58, 0x58, 0xFF}, // Grey35
{0x62, 0x62, 0x62, 0xFF}, // Grey39
{0x6C, 0x6C, 0x6C, 0xFF}, // Grey42
{0x76, 0x76, 0x76, 0xFF}, // Grey46
{0x80, 0x80, 0x80, 0xFF}, // Grey50
{0x8A, 0x8A, 0x8A, 0xFF}, // Grey54
{0x94, 0x94, 0x94, 0xFF}, // Grey58
{0x9E, 0x9E, 0x9E, 0xFF}, // Grey62
{0xA8, 0xA8, 0xA8, 0xFF}, // Grey66
{0xB2, 0xB2, 0xB2, 0xFF}, // Grey70
{0xBC, 0xBC, 0xBC, 0xFF}, // Grey74
{0xC6, 0xC6, 0xC6, 0xFF}, // Grey78
{0xD0, 0xD0, 0xD0, 0xFF}, // Grey82
{0xDA, 0xDA, 0xDA, 0xFF}, // Grey85
{0xE4, 0xE4, 0xE4, 0xFF}, // Grey89
{0xEE, 0xEE, 0xEE, 0xFF}  // Grey93
};
