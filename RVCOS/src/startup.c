#include <stdint.h>
#include <stddef.h>
#include "RVCOS.h"
#include <string.h>


extern uint8_t _erodata[];
extern uint8_t _data[];
extern uint8_t _edata[];
extern uint8_t _sdata[];
extern uint8_t _esdata[];
extern uint8_t _bss[];
extern uint8_t _ebss[];

// Adapted from https://stackoverflow.com/questions/58947716/how-to-interact-with-risc-v-csrs-by-using-gcc-c-code
__attribute__((always_inline)) inline uint32_t csr_mstatus_read(void){
    uint32_t result;
    asm volatile ("csrr %0, mstatus" : "=r"(result));
    return result;
}

__attribute__((always_inline)) inline void csr_mstatus_write(uint32_t val){
    asm volatile ("csrw mstatus, %0" : : "r"(val));
}

__attribute__((always_inline)) inline void csr_write_mie(uint32_t val){
    asm volatile ("csrw mie, %0" : : "r"(val));
}

__attribute__((always_inline)) inline void csr_enable_interrupts(void){
    asm volatile ("csrsi mstatus, 0x8");
}

__attribute__((always_inline)) inline void csr_disable_interrupts(void){
    asm volatile ("csrci mstatus, 0x8");
}

__attribute__((always_inline)) inline void set_gp(uint32_t addr) {
    asm volatile("sw gp, %0" : : "r"(addr));
}

#define MTIME_LOW       (*((volatile uint32_t *)0x40000008))
#define MTIME_HIGH      (*((volatile uint32_t *)0x4000000C))
#define MTIMECMP_LOW    (*((volatile uint32_t *)0x40000010))
#define MTIMECMP_HIGH   (*((volatile uint32_t *)0x40000014))
#define CONTROLLER      (*((volatile uint32_t *)0x40000018))
#define INTR_PEND_REG      (*((volatile uint32_t *)0x40000004))
#define INTR_ENAB_REG      (*((volatile uint32_t *)0x40000000))

char *_sbrk(int incr) {
  extern char _heapbase;		/* Defined by the linker */
  static char *heap_end;
  char *prev_end;

  if (heap_end == 0) {
    heap_end = &_heapbase;
  }
  prev_end = heap_end;

  //prev_end += incr;
  heap_end += incr;
  return (char*)prev_end;
}

void init(void){
    uint8_t *Source = _erodata;
    uint8_t *Base = _data < _sdata ? _data : _sdata;
    uint8_t *End = _edata > _esdata ? _edata : _esdata;

    while(Base < End){
        *Base++ = *Source++;
    }
    Base = _bss;
    End = _ebss;
    while(Base < End){
        *Base++ = 0;
    }

    INTR_ENAB_REG = 0x2;
    csr_write_mie(0x888);       // Enable all interrupt soruces
    csr_enable_interrupts();    // Global interrupt enable
    MTIMECMP_LOW = 1;
    MTIMECMP_HIGH = 0;
}

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

struct GCB{
    int dirty;
    TGraphicID gid;
    TGraphicType type;
    TGraphicState state;
    int height;
    int width;
    TPaletteIndex *buffer;
} ;

struct TCB{
    TThreadID tid;
    uint32_t *gp;
    TThreadState state; // different states: running, ready, dead, waiting, created
    TThreadPriority priority; // different priorities: high, normal, low
    uint32_t *sp;
    TMemorySize memsize;
    TThreadEntry entry;
    void *param;
    int ticks;
    TThreadID wait_id;
    TStatus ret_val;
    uint8_t* stack_base; // return value of malloc
    const TTextCharacter *buffer;
    TMemorySize writesize;
};

typedef void (*TUpcallPointer)(void *param);
extern volatile TUpcallPointer UpcallPointer;
extern volatile void *UpcallParam;

extern volatile int global;
extern volatile int tick_count;
extern volatile uint32_t controller_status;
extern volatile int numSleepers;
extern void schedule(); 
extern void enqueueThread(struct TCB* thread);
extern volatile int sleeperCursor;
extern TStatus RVCWriteText(const TTextCharacter *buffer, TMemorySize writesize);
extern TThreadID get_tp(void);
extern uint32_t * get_gp(void);
extern struct TCB** threadArray;
extern struct GCB** offscreenBufferArray;
extern struct ReadyQ *sleeperQ;
extern int removeRQ(struct ReadyQ *Q);
extern void insertRQ(struct ReadyQ *Q, int tid);
extern struct ReadyQ *writerQ;
extern struct ReadyQ *backgroundQ;
extern struct ReadyQ *largeSpriteQ;
extern struct ReadyQ *smallSpriteQ;
extern TStatus RVCWriteText1(const TTextCharacter *buffer, TMemorySize writesize);
extern volatile int num_of_threads;

extern volatile uint8_t *BackgroundData[5];  
extern volatile uint8_t *LargeSpriteData[64];
extern volatile uint8_t *SmallSpriteData[128];

// all from the discussion-11-19 code
extern volatile SColor *BackgroundPalettes[4];
extern volatile SColor *SpritePalettes[4];
extern volatile SBackgroundControl *BackgroundControls;// = (volatile SBackgroundControl *)0x500FF100;
extern volatile SLargeSpriteControl *LargeSpriteControls;// = (volatile SLargeSpriteControl *)0x500FF114;
extern volatile SSmallSpriteControl *SmallSpriteControls;// = (volatile SSmallSpriteControl *)0x500FF214;
extern volatile SVideoControllerMode *ModeControl;// = (volatile SVideoControllerMode *)0x500FF414;

extern volatile int upcallFlag;
extern volatile uint32_t *saved_sp;

struct ReadyQ{
    int* queue;
    int front;
    int rear;
    int size;
};

void video_interrupt_handler(void){
    if(INTR_PEND_REG & 0x2){
        INTR_PEND_REG = 0x2;
        struct TCB* curr = threadArray[get_tp()];
        int flag = 0;
	    while(writerQ->size){
            int threadTID = removeRQ(writerQ);
            struct TCB* thread = threadArray[threadTID];
            if(thread->writesize != 0){
                RVCWriteText1(thread->buffer, thread->writesize);
            }
            if(thread->state != RVCOS_THREAD_STATE_DEAD){
                thread->state = RVCOS_THREAD_STATE_READY;
                thread->buffer = NULL;
                thread->writesize = NULL;
                enqueueThread(thread);
            }
            if(thread->priority > curr->priority){
                flag = 1;
            }
            
	    }
        while(backgroundQ->size){
            int graphicID = removeRQ(backgroundQ);
            struct GCB* graphic = offscreenBufferArray[graphicID];
            graphic->state = RVCOS_GRAPHIC_STATE_ACTIVATED;
            memcpy((void *)BackgroundData[0],graphic->buffer,512*288);
            graphic->dirty = 0;
        }
        while(largeSpriteQ->size){
            int graphicID = removeRQ(largeSpriteQ);
            struct GCB* graphic = offscreenBufferArray[graphicID];
            graphic->state = RVCOS_GRAPHIC_STATE_ACTIVATED;
            memcpy((void *)LargeSpriteData[0],graphic->buffer,64*64);
            graphic->dirty = 0;
        }
        while(smallSpriteQ->size){
            int graphicID = removeRQ(smallSpriteQ);
            struct GCB* graphic = offscreenBufferArray[graphicID];
            graphic->state = RVCOS_GRAPHIC_STATE_ACTIVATED;
            memcpy((void *)SmallSpriteData[0],graphic->buffer,16*16);
            graphic->dirty = 0;
        }
        //upcallFlag = 1;
        //CallUpcall((void *)UpcallParam, (TUpcallPointer)UpcallPointer, (uint32_t *)get_gp(), (uint32_t *)saved_sp);
        //upcallFlag = 0;
        if(flag){
            schedule();
        }
        
    }
}

void timer_interrupt_handler()
{
    uint64_t NewCompare = (((uint64_t)MTIMECMP_HIGH)<<32) | MTIMECMP_LOW;
    NewCompare += 5;
    MTIMECMP_HIGH = NewCompare>>32;
    MTIMECMP_LOW = NewCompare;
    tick_count++;

    struct TCB* curr = threadArray[get_tp()];
    if(num_of_threads > 2){
        if(curr->state != RVCOS_THREAD_STATE_DEAD){
            curr->state = RVCOS_THREAD_STATE_READY;
        }
    }
    global++;
    controller_status = CONTROLLER;
    for(int i = 0; i < sleeperQ->size; i++){
        int threadId = removeRQ(sleeperQ);
        struct TCB* thread = threadArray[threadId];
        thread->ticks = thread->ticks - 1;      
        if(thread->ticks == 0){  // thread wakes up
            if(thread->state != RVCOS_THREAD_STATE_DEAD){
                thread->state = RVCOS_THREAD_STATE_READY;
                enqueueThread(thread);
            }
        }
        else{
            insertRQ(sleeperQ, threadId);
        }
    }
    if(num_of_threads > 2){
        curr->state = RVCOS_THREAD_STATE_READY;
        enqueueThread(curr);
        schedule();
    }
}



void c_interrupt_handler(uint32_t mcause){
    switch(mcause){
        case 0x80000007: return timer_interrupt_handler();
        case 0x8000000B: return video_interrupt_handler();
    }
}

