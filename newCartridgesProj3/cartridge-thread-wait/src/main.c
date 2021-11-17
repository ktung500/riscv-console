#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include "RVCOS.h"


#define EXPECTED_RETURN             5

TThreadID MainThreadID, LowThreadID, HighThreadID;


int abs(int v) 
{
  return v * ((v>0) - (v<0));
}

void swap(char *x, char *y) {
    char t = *x; *x = *y; *y = t;
}
 
// Function to reverse `buffer[i…j]`
char* reverse(char *buffer, int i, int j)
{
    while (i < j) {
        swap(&buffer[i++], &buffer[j--]);
    }
 
    return buffer;
}
 
// Iterative function to implement `itoa()` function in C
char* itoa(int value, char* buffer, int base)
{
    // invalid input
    if (base < 2 || base > 32) {
        return buffer;
    }
 
    // consider the absolute value of the number
    int n = abs(value);
 
    int i = 0;
    while (n)
    {
        int r = n % base;
 
        if (r >= 10) {
            buffer[i++] = 65 + (r - 10);
        }
        else {
            buffer[i++] = 48 + r;
        }
 
        n = n / base;
    }
 
    // if the number is 0
    if (i == 0) {
        buffer[i++] = '0';
    }
 
    // If the base is 10 and the value is negative, the resulting string
    // is preceded with a minus sign (-)
    // With any other base, value is always considered unsigned
    if (value < 0 && base == 10) {
        buffer[i++] = '-';
    }
 
    buffer[i] = '\0'; // null terminate string
 
    // reverse the string and return it
    return reverse(buffer, 0, i - 1);
}


void WriteString(const char *str){
    const char *Ptr = str;
    while(*Ptr){
        Ptr++;
    }
    RVCWriteText(str,Ptr-str);
}

TThreadReturn LowPriorityThread(void *param){
    TThreadID CurrentThreadID;
    TThreadState ThreadState;
    TThreadReturn ReturnValue;

    WriteString("Low Thread:    ");
    RVCThreadID(&CurrentThreadID);
    if(CurrentThreadID != LowThreadID){
        WriteString("Invalid Thread ID\n");
        return 1;
    }
    WriteString("Valid Thread ID\n");
    WriteString("Checking Main: ");
    RVCThreadState(MainThreadID,&ThreadState);
    if(RVCOS_THREAD_STATE_WAITING != ThreadState){
        // WriteString("\n main state: ");
        // char buff[20];
        // uint32_t id = ThreadState;
        // itoa(id, buff, 10);
        // WriteString(buff);
        // WriteString(" ");
        WriteString("Invalid State\n");
        return 1;
    }
    WriteString("Valid State\n");
    WriteString("Checking This: ");
    RVCThreadState(LowThreadID,&ThreadState);
    if(RVCOS_THREAD_STATE_RUNNING != ThreadState){
        // WriteString("\n low state: ");
        // char buff[20];
        // uint32_t id = ThreadState;
        // itoa(id, buff, 10);
        // //snprintf(buff, 10, "%d", id);
        // WriteString(buff);
        // WriteString(" ");
        WriteString("Invalid State\n");
        return 1;
    }
    WriteString("Valid State\n");
    RVCThreadActivate(HighThreadID);
    RVCThreadWait(HighThreadID,&ReturnValue,RVCOS_TIMEOUT_INFINITE);
    WriteString("Low Awake:     ");
    if(EXPECTED_RETURN != ReturnValue){
        // WriteString("\n high returnval: ");
        // char buff[20];
        // uint32_t id = ReturnValue;
        // itoa(id, buff, 10);
        // WriteString(buff);
        // WriteString(" ");
        WriteString("Invalid Return\n");
        return 0;
    }
    WriteString("Valid Return\n");
    RVCThreadTerminate(LowThreadID,ReturnValue);

    return 0;
}

TThreadReturn HighPriorityThread(void *param){
    TThreadID CurrentThreadID;
    TThreadState ThreadState;
    WriteString("High Thread:   ");
    RVCThreadID(&CurrentThreadID);
    if(CurrentThreadID != HighThreadID){
        WriteString("Invalid Thread ID\n");
        return 1;
    }
    WriteString("Valid Thread ID\n");
    WriteString("Checking Main: ");
    RVCThreadState(MainThreadID,&ThreadState);
    if(RVCOS_THREAD_STATE_WAITING != ThreadState){
        // WriteString("\n main state: ");
        // char buff[1];
        // uint32_t id = ThreadState;
        // itoa(id, buff, 10);
        // //snprintf(buff, 10, "%d", id);
        // WriteString(buff);
        // WriteString("\n");
        WriteString("Invalid State\n");
        return 1;
    }
    WriteString("Valid State\n");
    WriteString("Checking This: ");
    RVCThreadState(HighThreadID,&ThreadState);
    if(RVCOS_THREAD_STATE_RUNNING != ThreadState){
        // WriteString("\n high state: ");
        // char buff[1];
        // uint32_t id = ThreadState;
        // itoa(id, buff, 10);
        // //snprintf(buff, 10, "%d", id);
        // WriteString(buff);
        // WriteString("\n");
        WriteString("Invalid State\n");
        return 1;
    }
    WriteString("Valid State\n");
    return EXPECTED_RETURN;
}

int main(){
    TThreadID CurrentThreadID;
    TThreadState ThreadState;
    TThreadReturn ReturnValue;

    WriteString("Main Thread:   ");
    RVCThreadID(&MainThreadID);

    // char buff[1];
    // uint32_t id = MainThreadID;
    // itoa(id, buff, 10);
    // WriteString(buff);
    
    RVCThreadState(MainThreadID,&ThreadState);
    if(RVCOS_THREAD_STATE_RUNNING != ThreadState){
        WriteString("Invalid State\n");
        return 1;
    }
    WriteString("Valid State\n");
    RVCThreadCreate(HighPriorityThread,NULL,2048,RVCOS_THREAD_PRIORITY_HIGH,&HighThreadID);
    WriteString("High Created:  ");

    // char buff1[1];
    // uint32_t id1 = HighThreadID;
    // itoa(id1, buff1, 10);
    // WriteString(buff1);

    RVCThreadState(HighThreadID,&ThreadState);
    if(RVCOS_THREAD_STATE_CREATED != ThreadState){
        WriteString("Invalid State\n");
        return 1;
    }
    WriteString("Valid State\n");
    RVCThreadCreate(LowPriorityThread,NULL,2048,RVCOS_THREAD_PRIORITY_LOW,&LowThreadID);
    WriteString("Low Created:   ");
    
    // char buff2[1];
    // uint32_t id2 = LowThreadID;
    // itoa(id2, buff2, 10);
    // WriteString(buff2);

    RVCThreadState(LowThreadID,&ThreadState);
    if(RVCOS_THREAD_STATE_CREATED != ThreadState){
        WriteString("Invalid State\n");
        return 1;
    }
    WriteString("Valid State\n");
    RVCThreadActivate(LowThreadID);
    WriteString("Low Activated: ");
    RVCThreadState(LowThreadID,&ThreadState);
    if((RVCOS_THREAD_STATE_READY != ThreadState)&&(RVCOS_THREAD_STATE_WAITING != ThreadState)){
        WriteString("Invalid State\n");
        return 1;
    }
    WriteString("Valid State\n");
    RVCThreadWait(LowThreadID,&ReturnValue,RVCOS_TIMEOUT_INFINITE);
    
    WriteString("Checking Low:  ");
    if(EXPECTED_RETURN != ReturnValue){
        // WriteString("\n low retval: ");
        // char buff[1];
        // uint32_t id = ReturnValue;
        // itoa(id, buff, 10);
        // //snprintf(buff, 10, "%d", id);
        // WriteString(buff);
        // WriteString("\n");
        WriteString("Invalid Return\n");
        return 1;
    }
    RVCThreadState(LowThreadID,&ThreadState);
    if(RVCOS_THREAD_STATE_DEAD != ThreadState){
        // WriteString("\n low state: ");
        // char buff[1];
        // uint32_t id = ThreadState;
        // itoa(id, buff, 10);
        // //snprintf(buff, 10, "%d", id);
        // WriteString(buff);
        // WriteString("\n");
        WriteString("Invalid State\n");
        return 1;
    }
    WriteString("Valid Return and State\n");

    WriteString("Checking High: ");
    RVCThreadState(HighThreadID,&ThreadState);
    if(RVCOS_THREAD_STATE_DEAD != ThreadState){
        // WriteString("\n high state: ");
        // char buff[1];
        // uint32_t id = ThreadState;
        // itoa(id, buff, 10);
        // //snprintf(buff, 10, "%d", id);
        // WriteString(buff);
        // WriteString("\n");
        WriteString("Invalid State\n");
        return 1;
    }
    WriteString("Valid State\n");
    RVCThreadID(&CurrentThreadID);
    if(CurrentThreadID != MainThreadID){
        WriteString("Invalid Main Thread ID\n");
        return 1;
    }
    WriteString("Main Exiting\n");
    
    return 0;
}
