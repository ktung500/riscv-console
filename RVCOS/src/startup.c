#include <stdint.h>
#include <stddef.h>
#include "RVCOS.h"


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
    const TTextCharacter *buffer;
    TMemorySize writesize;
};

extern volatile int global;
extern volatile int tick_count;
extern volatile uint32_t controller_status;
//extern struct TCB* sleepers[256];
extern volatile int numSleepers;
extern void schedule(); 
extern void enqueueThread(struct TCB* thread);
extern volatile int sleeperCursor;
extern TStatus RVCWriteText(const TTextCharacter *buffer, TMemorySize writesize);
extern TThreadID get_tp(void);
extern struct TCB** threadArray;
extern struct ReadyQ *sleeperQ;
extern int removeRQ(struct ReadyQ *Q);
extern void insertRQ(struct ReadyQ *Q, int tid);
extern struct ReadyQ *writerQ;
extern TStatus RVCWriteText1(const TTextCharacter *buffer, TMemorySize writesize);
extern volatile int num_of_threads;

struct ReadyQ{
    int* queue;
    int front;
    int rear;
    int size;
};


void video_interrupt_handler(void){
    if(INTR_PEND_REG & 0x2){
        // video interrupt
        //RVCWriteText("video interrupt\n",15);
        struct TCB* curr = threadArray[get_tp()];
        int flag = 0;
	    while(writerQ->size != 0){
            int threadTID = removeRQ(writerQ);
            struct TCB* thread = threadArray[threadTID];
		    RVCWriteText1(thread->buffer, thread->writesize);
            thread->state = RVCOS_THREAD_STATE_READY;
            thread->buffer = NULL;
            thread->writesize = NULL;
            enqueueThread(thread);
            if(thread->priority > curr->priority){
                flag = 1;
            }
            
	    }
        INTR_PEND_REG = 0x2;
        if(flag){
                schedule();
        }
        
    }
}



void c_interrupt_handler(void){
    uint64_t NewCompare = (((uint64_t)MTIMECMP_HIGH)<<32) | MTIMECMP_LOW;
    NewCompare += 2;
    MTIMECMP_HIGH = NewCompare>>32;
    MTIMECMP_LOW = NewCompare;
    tick_count++;

    // need to make sure its a timer interrupt
    // save mepc
    struct TCB* curr = threadArray[get_tp()];
    if(num_of_threads > 1){
        curr->state = RVCOS_THREAD_STATE_READY;
        //RVCWriteText1("main enqueued\n",14);
    //    enqueueThread(curr);
    }

    global++;
    controller_status = CONTROLLER;
    for(int i = 0; i < sleeperQ->size; i++){
        int threadId = removeRQ(sleeperQ);
        struct TCB* thread = threadArray[threadId];
        thread->ticks = thread->ticks - 1;      
        if(thread->ticks == 0){  // thread wakes up
            //sleepers[i] == NULL;
            thread->state = RVCOS_THREAD_STATE_READY;
            //numSleepers--;
            enqueueThread(thread);
            //schedule(); 
            // need to handle decrementing the numSleepers correctly
        }
        else{
            insertRQ(sleeperQ, threadId);
        }
    }
    if(num_of_threads > 1){
        schedule();
    }
    
    
    /*if(numSleepers == 0){
        schedule();
    }*/
}

