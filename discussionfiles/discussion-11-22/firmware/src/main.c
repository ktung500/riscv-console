#include <stdint.h>
#include <string.h>

volatile int global = 42;
volatile uint32_t controller_status = 0;
volatile uint32_t *saved_sp;
volatile uint32_t *cartridge_gp;

typedef void (*TFunctionPointer)(void);
typedef void (*TUpcallPointer)(void *param);
void enter_cartridge(void);
#define MTIME_LOW       (*((volatile uint32_t *)0x40000008))
#define MTIME_HIGH      (*((volatile uint32_t *)0x4000000C))
#define MTIMECMP_LOW    (*((volatile uint32_t *)0x40000010))
#define MTIMECMP_HIGH   (*((volatile uint32_t *)0x40000014))
#define CONTROLLER      (*((volatile uint32_t *)0x40000018))
#define CART_STAT_REG   (*(volatile uint32_t *)0x4000001C)

volatile TUpcallPointer UpcallPointer = NULL;
volatile void *UpcallParam = NULL;

void CallUpcall(void *param, TUpcallPointer upcall, uint32_t *gp, uint32_t *sp);

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

int main() {
    saved_sp = &controller_status;
    while(1){
        if(CART_STAT_REG & 0x1){


            csr_write_mie(0x888);       // Enable all interrupt soruces
            csr_enable_interrupts();    // Global interrupt enable
            uint64_t NewCompare = (((uint64_t)MTIME_HIGH)<<32) | MTIME_LOW;
            NewCompare++;
            MTIMECMP_HIGH = NewCompare>>32;
            MTIMECMP_LOW = NewCompare;
    
            enter_cartridge();
        }
    }
    return 0;
}

uint32_t c_syscall_handler(uint32_t p1,uint32_t p2,uint32_t p3,uint32_t p4,uint32_t p5,uint32_t code){
    if(!code){
        cartridge_gp = (void *)p1;
    }
    if(24 == code){
        UpcallPointer = (TUpcallPointer)p1;
        UpcallParam = (void *)p2;
    }
    return code + 1;
}

void c_interrupt_handler(void){
    uint64_t NewCompare = (((uint64_t)MTIMECMP_HIGH)<<32) | MTIMECMP_LOW;
    NewCompare += 10000;
    MTIMECMP_HIGH = NewCompare>>32;
    MTIMECMP_LOW = NewCompare;
    global++;
    controller_status = CONTROLLER;
    if(UpcallPointer){
        CallUpcall((void *)UpcallParam, (TUpcallPointer)UpcallPointer, (uint32_t *)cartridge_gp, (uint32_t *)saved_sp);
    }
}