/*
 * TODO:
 * Implement 'yield' function that places current thread onto waiting queue and calls the scheduler
*/
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <setjmp.h>
#define QUEUE_LEN 50

typedef enum {false, true} bool;
struct sigaction* sa;

int ping();
int pong();
int pop();
void callScheduler();

// Thread has a pointer to a function to run, a stack to store local variables, and an environment to save its state
struct Thread {
    int (*func)();
    void* stack;
    jmp_buf env;
    int input;
    bool in_progress;
};
struct Thread* running;

int freeThread(struct Thread* t) {
    free(t->stack);
    free(t);
    return 0;
}

// Entire structure for queuing system should be in heap memory so that it can be accessed regardless of which thread calls the scheduler 
struct Queue {
    int head;
    int tail;
    struct Thread* arr[50];
}; 
struct Queue* ready_q;

// Queue initialization
struct Queue* createQueue() {
    struct Queue* queue = (struct Queue*)malloc(sizeof(struct Queue));
    queue->head = 0;
    queue->tail = 0;
    return queue;
}

// Queue manipulation functions: pop, push, empty, full
struct Thread* popQueue(struct Queue* q) {
    struct Thread* popReturn;
    if (q->head == q->tail) {
        printf("nothing to return\n");
        return NULL; // Error, no items in queue
    } else {
        popReturn = q->arr[q->tail];
        q->tail = (q->tail + 1) % QUEUE_LEN;
        return popReturn;
    }
}

int pushQueue(struct Queue* q, struct Thread* t) {
    if ((q->head + 1) % QUEUE_LEN == q->tail) {
        return -1; // Error, queue full
    } else {
        q->arr[q->head] = t;
        q->head = (q->head + 1) % QUEUE_LEN;
    } 
    return 0;
}

bool queueEmpty(struct Queue* q) {
    return q->head == q->tail;
}

bool queueFull(struct Queue* q) {
    return ((q->head + 1) % QUEUE_LEN == q->tail);
}


// Allocate stack and bind function to new thread 
void* createThread(int (*func)()) {
    struct Thread* new_thread = (struct Thread*)malloc(sizeof(struct Thread));
    new_thread->stack = malloc(0x10000); // give the frame ~64k of stack memory 
    new_thread->func = func;
    // new_thread->input = input; 
    new_thread->in_progress = false;
    return new_thread;
}

// Starts a threads function and points rbp and rsp bottom of stack, calls scheduler when thread returns
void startThread(struct Thread* t) {
    t->in_progress = true;
    asm volatile("pushq %%r10\n\t" // save caller regisers
                 "pushq %%r11\n\t"
                 "pushq %%rdi\n\t"
        
                 "movq %%rbp, %%r12\n\t" // save current base and stack pointer
                 "movq %%rsp, %%r13\n\t"
        
                 "lea (0x10000-0x8)(%0), %%rbp\n\t" // point rbp and rsp to the end of the frame
                 "lea (0x10000-0x8)(%0), %%rsp\n\t"
        
                 "call *%1\n\t"  // call function (pushes return address onto top of stack)
        
                 "movq %%r13, %%rsp\n\t" // restore original base and stack pointers
                 "movq %%r12, %%rbp\n\t"
        
                 "popq %%rdi\n\t" // restore caller registers
                 "popq %%r11\n\t"
                 "popq %%r10\n\t"
                 :
                 :"r" (t->stack), "r" (t->func));
    freeThread(t);
    printf("Thread dealocated\n");
    running = NULL;
    callScheduler();
}


// Implements a simple round robin scheduler: starting inactive threads, resuming active threads, and saving the state of the previous thread
void callScheduler() {
    struct Thread* next_thread;
    struct Thread* old_thread;

    // Push registers onto stack
    asm volatile ("pushq %rax\n\t"
                  "pushq %rbx\n\t"
                  "pushq %rcx\n\t"
                  "pushq %rdx\n\t"
                  "pushq %rsi\n\t"
                  "pushq %r8\n\t"
                  "pushq %r9\n\t"
                  "pushq %r12\n\t"
                  "pushq %r13\n\t"
                  "pushq %r14\n\t"
                  "pushq %r15");
    
    // If the queue is empty, just jump out of the signal handler
    if(!queueEmpty(ready_q)) {
        old_thread = running; 
        //printf("q->head = %d, q->tial = %d\n", ready_q->head, ready_q->tail);
        next_thread = popQueue(ready_q); 
        if (next_thread == NULL) {
            printf("error popping q\n"); 
        }
        if (old_thread != NULL) {
            pushQueue(ready_q, old_thread);
        }
        running = next_thread;

        // start up the thread if it hasn't begun executing
        if (!running->in_progress) {
            printf("about to start next thread\n");
            if (old_thread == NULL) {
                startThread(next_thread); 
            } else {
                if (!setjmp(old_thread->env)) {
                    startThread(next_thread); 
                }
            }
        } else {
            printf("about to reusme next thread\n");
            if (old_thread == NULL) {
                longjmp(next_thread->env, 1);
            } else {
                if (!setjmp(old_thread->env)) {
                    longjmp(next_thread->env, 1);
                }
            }
        }
    } else {
        ; //TBI, run null job 
    }

    // Restore registers from stack and return
    asm volatile ("popq %r15\n\t"
                  "popq %r14\n\t"
                  "popq %r13\n\t"
                  "popq %r12\n\t"
                  "popq %r9\n\t"
                  "popq %r8\n\t"
                  "popq %rsi\n\t"
                  "popq %rdx\n\t"
                  "popq %rcx\n\t"
                  "popq %rbx\n\t"
                  "popq %rax\n\t"
                  );
    return;
}

// Reset alarm and call scheduler
void alarm_handler(int signum) {
    printf("set next alarm\n");
    memset(sa, 0, sizeof(sigaction)); 
    sa->sa_handler = alarm_handler;
    sa->sa_flags = SA_NODEFER;
    sigaction(SIGALRM, sa, NULL);
    alarm(1);
    callScheduler();
}

int main() {
    // Set up signal handler and disable interrupt defer flag
    sa = (struct sigaction*)malloc(sizeof(struct sigaction));
    memset(sa, 0, sizeof(sigaction)); 
    sa->sa_handler = alarm_handler;
    sa->sa_flags = SA_NODEFER;
    sigaction(SIGALRM, sa, NULL);

    // Create and populate thread queue
    ready_q = createQueue();
    printf("made ready q\n");
    pushQueue(ready_q, createThread(ping));
    pushQueue(ready_q, createThread(pong));
    pushQueue(ready_q, createThread(pop));

    printf("Set the initial alarm\n");
    alarm(1);

    running = NULL;
    callScheduler();

    return 0;
}

int ping() {
    for (int i = 3; i > 0; i--) {
        printf("PING %d more times\n", i-1);
        pause();
    }
    return 0;
}

int pong() {
    for(;;) {
        printf("PONG\n");
        pause();      
    }
    return 0;
}

int pop() {
    for(;;) {
        printf("POP\n");
        pause();      
    }
    return 0;
}
