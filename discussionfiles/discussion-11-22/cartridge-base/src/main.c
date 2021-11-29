#include <stdint.h>
#include <string.h>
#include "RVCOS.h"

volatile int UpcallCount = 0;

TThreadReturn Upcall(void *param){
    UpcallCount++;
    return 0;
}

int main() {
    RVCSetVideoUpcall(Upcall,NULL);
    while(UpcallCount < 10){
        memset(((void *)0x500fe800),'0' + UpcallCount,64*36);
    };

    return 0;
}
