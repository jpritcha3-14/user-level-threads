// Alarm handler should just reset the alarm and then call a seperate scheduler function.  This decouples the scheduler from the alarm
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

struct Thread {
    int (*func)();
    struct Thread* next;
    int input;
    jmp_buf env;
    bool in_progress;
    void* stack;
};
struct Thread* running;

// entire structure for queuing system should be in heap memory so that all threads can access
struct Queue {
    int head;
    int tail;
    struct Thread* arr[50];
}; 
struct Queue* ready_q;

// queue initialization
struct Queue* createQueue() {
    struct Queue* queue = (struct Queue*)malloc(sizeof(struct Queue));
    queue->head = 0;
    queue->tail = 0;
    return queue;
}

struct Thread* popReturn;
struct Thread* popQueue(struct Queue* q) {
    if (q->head == q->tail) {
        printf("nothing to return\n");
        return NULL; // Error, no items in queue
    } else {
        popReturn = q->arr[q->tail];
        q->tail = (q->tail + 1) % QUEUE_LEN;
        //printf("q->head = %d, q->tial = %d\n", q->head, q->tail);
        //printf("popped thread @ address: %p\n", popReturn);
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


void* createThread(int (*func)()) {
    struct Thread* new_thread = (struct Thread*)malloc(sizeof(struct Thread));
    new_thread->stack = malloc(0x10000); // give the frame ~64k of stack memory 
    new_thread->func = func;
    // new_thread->input = input; 
    // Stack can be initialized
    //((uint64_t*)(new_thread->stack))[0] = 42;
    new_thread->in_progress = false;
    return new_thread;
}

void startThread(struct Thread* t) {
    t->in_progress = true;
    asm volatile("pushq %%r10\n\t" // save caller regisers
                 "pushq %%r11\n\t"
                 "pushq %%rdi\n\t"
        
                 "movq %%rbp, %%r12\n\t" // save current base and stack pointer
                 "movq %%rsp, %%r13\n\t"
        
                 "lea (0x10000-0x8)(%0), %%rbp\n\t" // point rbp and rsp to the end of the frame
                 "lea (0x10000-0x8)(%0), %%rsp\n\t"
        
                 "call *%1\n\t"  // call function (pushes return value onto top of stack)
        
                 "movq %%r13, %%rsp\n\t" // restore original base and stack pointers
                 "movq %%r12, %%rbp\n\t"
        
                 "popq %%rdi\n\t" // restore caller registers
                 "popq %%r11\n\t"
                 "popq %%r10\n\t"
                 :
                 :"r" (t->stack), "r" (t->func));
    t->in_progress = false;
    //Call context switcher
}

void alarm_handler(int signum) {
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


    // reset alarm
    printf("set next alarm\n");
    memset(sa, 0, sizeof(sigaction)); 
    sa->sa_handler = alarm_handler;
    sa->sa_flags = SA_NODEFER;
    sigaction(SIGALRM, sa, NULL);
    alarm(1);
    
    // If the queue is empty, just jump out of the signal handler
    if(!queueEmpty(ready_q)) {
        old_thread = running; 
        printf("q->head = %d, q->tial = %d\n", ready_q->head, ready_q->tail);
        next_thread = popQueue(ready_q); 
        if (next_thread == NULL) {
            printf("error popping q\n"); 
        }
        pushQueue(ready_q, old_thread);
        running = next_thread;

        // start up the thread if it hasn't begun executing
        if (!running->in_progress) {
            if (!setjmp(old_thread->env)) {
                printf("about to start next thread\n");
                startThread(next_thread); 
            }
        } else {
            if (!setjmp(old_thread->env)) {
                // Return to context of next thread
                printf("about to resume next thread\n");
                longjmp(next_thread->env, 1);
            }
        }
    }

    // restore registers from stack and return
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
    pushQueue(ready_q, createThread(pong));
    pushQueue(ready_q, createThread(pop));
    printf("pushed pong thread onto ready q\n");
    printf("q->head = %d, q->tial = %d\n", ready_q->head, ready_q->tail);
    running = createThread(ping);

    printf("Set the initial alarm\n");
    alarm(1);

    startThread(running);

    return 0;
}

int ping() {
    for (int i = 5; i > 0; i--) {
        printf("starting ping in %d\n", i);
        pause();
    }
    for(;;) {
        printf("PING\n");
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


int free_threads(struct Thread* head) {
    struct Thread* cur_t = head;
    struct Thread* next_t = head;
    while (cur_t != NULL) {
        free(cur_t->stack);
        next_t = cur_t->next;
        free(cur_t);
        cur_t = next_t;
    }
    return 1;
}
