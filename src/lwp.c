#include "../include/lwp.h"
#include <stdlib.h> 
#include <string.h> 
#include <stdio.h>
#include <sys/mman.h> 
#include <sys/resource.h>
#include <unistd.h> 

#define DEFAULT_STACK_SIZE (8 * 1024 * 1024)

// Global Variables 
tid_t next_tid = 1;
thread curr_lwp = NULL;     // Currently running thread
thread all_lwps = NULL;     // Head of the list of all LWPs (for tid2thread)
scheduler curr_sched = NULL; // Currently active scheduler

// FIFO Library Queues (Using lib_two for queue management)
// Waiting: Simple singly linked list  of LWPs blocked in lwp_wait()
thread waiting_head = NULL;
thread waiting_tail = NULL;

// Terminated: Simple singly linked list of LWPs ready to be reaped
thread terminated_head = NULL;
thread terminated_tail = NULL;


// External assembly function
extern void swap_rfiles(rfile *old, rfile *new);
extern void lwp_wrap(lwpfun fun, void *arg);

/* Queue Helper Functions (Using lib_two) */

// Enqueues a thread onto a singly linked list (FIFO)
static void enqueue(thread *head, thread *tail, thread new_lwp) {
    new_lwp->lib_two = NULL; // Reset next pointer
    if (*tail) {
        (*tail)->lib_two = new_lwp;
    } else {
        *head = new_lwp;
    }
    *tail = new_lwp;
}

// Dequeues the oldest thread from a singly linked list (FIFO)
static thread dequeue(thread *head, thread *tail) {
    thread victim = *head;
    if (victim) {
        *head = victim->lib_two;
        if (!(*head)) {
            *tail = NULL;
        }
        victim->lib_two = NULL; // Clear link
    }
    return victim;
}

/* --- Round Robin Scheduler Functions (Unmodified) --- */

thread rr_head = NULL; // Head of the circular linked list
int rr_cnt = 0;

void rr_init(void) { 
    rr_head = NULL;
    rr_cnt = 0;
}

// Add 'new' to the end of the circular list
void rr_admit(thread new) {
    if(rr_head){
        //the last thread is the previous to the head
        thread prev = rr_head->sched_two;
        prev->sched_one = new;
        // the next after the last thread is the head
        // the previous of new shuold be the old last thread
        new->sched_one = rr_head;
        new->sched_two = prev;
        rr_head->sched_two = new;
    }
    else{
        // if rr_head was NULL, new is the only element in the list
        rr_head = new;
        // if there is only 1 element, its next and prev pointers are to itself.
        rr_head->sched_one = rr_head;
        rr_head->sched_two = rr_head;
    }
    rr_cnt++;
}

// Remove 'victim' from the circular list
void rr_remove(thread victim) {
    if (rr_cnt == 0) return; // Empty list
    
    if (rr_head == victim) {
        if (rr_cnt == 1) {
            rr_head = NULL;
        } else {
            thread next = rr_head->sched_one;
            thread prev = rr_head->sched_two;
            prev->sched_one = next;
            next->sched_two = prev;
            rr_head = next; // Move head to next element
        }
    } else {
        thread curr = rr_head->sched_one;
        while (curr != rr_head) { // Loop until back at start
            if (curr == victim) {
                thread next = curr->sched_one;
                thread prev = curr->sched_two;
                next->sched_two = prev;
                prev->sched_one = next;
                break;
            }
            curr = curr->sched_one;
        }
    }
    
    // Clear victim's scheduler pointers and decrement count
    if (victim->sched_one || victim->sched_two) {
        victim->sched_one = victim->sched_two = NULL;
        rr_cnt--;
    }
}

// Get the thread from the head, move it to the tail, and return it.
thread rr_next(void) {
    thread next = rr_head;
    if (rr_head) {
        // Just advance the head pointer in the circular list
        rr_head = rr_head->sched_one;
    }
    // If the list was empty or now empty, this returns NULL
    return next;
}

// assumes list only has runnable threads, and that 
// threads are removed when terminated, decrementing rr_cnt
int rr_qlen(void) {
    return rr_cnt;
}

// uses sched_one as 'next' and sched_two as 'prev'
scheduler RoundRobin = &((struct scheduler) {
    rr_init, NULL, rr_admit, rr_remove, rr_next, rr_qlen
});

/* LWP Functions */

// returns NO_THREAD on failure 
extern tid_t lwp_create(lwpfun function, void *argument) {
    thread new_lwp;
    size_t stack_size;
    unsigned long *stack_base;
    unsigned long *tos; 

    new_lwp = (thread)malloc(sizeof(struct threadinfo_st));
    if (!new_lwp) {
        perror("malloc");
        return NO_THREAD;
    }
    memset(new_lwp, 0, sizeof(struct threadinfo_st));

    // Stack size calculation
    struct rlimit rl;
    if (getrlimit(RLIMIT_STACK, &rl) == 0 && rl.rlim_cur != RLIM_INFINITY) {
        stack_size = (size_t)rl.rlim_cur;
    } else {
        stack_size = DEFAULT_STACK_SIZE; 
    }
    long page_size = sysconf(_SC_PAGE_SIZE);
    if (page_size == -1) page_size = 4096;
    stack_size = (stack_size + page_size - 1) & ~(page_size - 1);
    new_lwp->stacksize = stack_size;

    // Allocate Stack 
    stack_base = (unsigned long *)mmap(NULL, stack_size, PROT_READ | PROT_WRITE, 
                                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (stack_base == MAP_FAILED) {
        free(new_lwp);
        return NO_THREAD;
    }
    new_lwp->stack = stack_base;

    // Initialize Context
    new_lwp->state.fxsave = FPU_INIT;
    tos = (unsigned long *)((char *)new_lwp->stack + new_lwp->stacksize);
    tos--; *tos = (unsigned long)lwp_exit; // Push lwp_exit
    tos--; *tos = 17;                     // Push RBP
    unsigned long aligned_rsp = (unsigned long)tos;
    aligned_rsp = (aligned_rsp & ~0xFUL);
    tos = (unsigned long *)aligned_rsp;
    *tos = (unsigned long)lwp_wrap; 

    // Set Registers
    new_lwp->state.rsp = (unsigned long)tos;
    new_lwp->state.rbp = 0; // initialize RBP 
    new_lwp->state.rdi = (unsigned long)function;
    new_lwp->state.rsi = (unsigned long)argument;
    
    // Finalize Context
    new_lwp->tid = next_tid++;
    new_lwp->status = LWP_LIVE;

    // Link to Global All-LWP List
    new_lwp->lib_one = all_lwps;
    all_lwps = new_lwp;

    // Admit to Scheduler
    if (curr_sched == NULL) {
        curr_sched = RoundRobin;
        if (curr_sched->init) {
            curr_sched->init();
        }
    }
    curr_sched->admit(new_lwp);

    return new_lwp->tid;
}

// LWP wrapper function: calls user fun and ensures proper termination
extern void lwp_wrap(lwpfun fun, void *arg) {
    int rval;
    rval = fun(arg); // Call user thread function
    lwp_exit(rval);  // Terminate with the return value
}

// Terminates calling lwp. This function yields control to next thread
extern void lwp_exit(int status){
    thread waiting_thread;
    
    // 1. Set the thread's termination status
    curr_lwp->status = MKTERMSTAT(LWP_TERM, status);

    // 2. Remove the LWP from the runnable queue
    curr_sched->remove(curr_lwp);

    // 3. Handle Waiters: Check the waiting queue
    waiting_thread = dequeue(&waiting_head, &waiting_tail);
    
    if (waiting_thread != NULL) { 
        // A thread is waiting; unblock it.
        
        // Link the terminated LWP to the waiting thread's 'exited' pointer
        waiting_thread->exited = curr_lwp;
        
        // Re-admit the formerly waiting thread back to the scheduler
        curr_sched->admit(waiting_thread);
        
    } else {
        // No thread is waiting; enqueue the current LWP onto the terminated list.
        enqueue(&terminated_head, &terminated_tail, curr_lwp);
    }

    // 4. Yield control. This function does not return.
    lwp_yield();
}

// returns the tid from the current LWP's context, or NO_THREAD if curr_lwp is NULL
extern tid_t lwp_gettid(void){
    if(curr_lwp){
        return curr_lwp->tid;
    }
    else{
        return NO_THREAD;
    }
}

// yields control to the next LWP
extern void lwp_yield(void){
    thread next_lwp;
    thread old_lwp;
    
    if (curr_sched == NULL || curr_lwp == NULL) {
        exit(EXIT_FAILURE); 
    }
    
    next_lwp = curr_sched->next();

    // Handle System Termination
    if (next_lwp == NULL) {
        int term_status = LWPTERMSTAT(curr_lwp->status);
        if (curr_sched->shutdown) {
            curr_sched->shutdown();
        }
        exit(term_status); 
    }

    // Context Switch
    old_lwp = curr_lwp;
    curr_lwp = next_lwp; 
    swap_rfiles(&old_lwp->state, &curr_lwp->state);
}

extern void lwp_start(void){
    thread original_thread;
    
    if (curr_lwp != NULL) {
        return;
    }

    original_thread = (thread)malloc(sizeof(struct threadinfo_st));
    if (!original_thread) {
        perror("malloc: Failed to allocate context for original thread");
        exit(EXIT_FAILURE);
    }
    memset(original_thread, 0, sizeof(struct threadinfo_st));

    original_thread->tid = next_tid++;
    original_thread->status = LWP_LIVE;
    original_thread->stack = NULL; 
    original_thread->stacksize = 0;
    
    curr_lwp = original_thread;
    
    swap_rfiles(&curr_lwp->state, NULL);

    if (curr_sched == NULL) {
        curr_sched = RoundRobin;
        if (curr_sched->init) {
            curr_sched->init();
        }
    }
    
    curr_sched->admit(curr_lwp);

    original_thread->lib_one = all_lwps;
    all_lwps = original_thread;
    
    lwp_yield();
}

// waits for and reaps a terminated LWP
extern tid_t lwp_wait(int *status){
    thread term_lwp;

    // 1. Check terminated list
    term_lwp = dequeue(&terminated_head, &terminated_tail);
    
    if (term_lwp) {
        // Found a terminated LWP to reap (fast path)
        goto reap_thread;
    }

    // 2. Check for deadlock (only happens if the calling thread is the only runnable thread)
    // The original system thread can't block itself if it is the only one.
    if (curr_sched->qlen() <= 1 && curr_lwp->stack != NULL) {
         // This is a subtle condition. If the only runnable thread is the caller, and it's not
         // the original system thread (stack!=NULL), it can't wait for anything else to terminate.
         return NO_THREAD;
    }

    // 3. Block: Remove from runnable queue and add to waiting queue
    curr_sched->remove(curr_lwp);
    enqueue(&waiting_head, &waiting_tail, curr_lwp);
    
    // Switch to another thread (lwp_yield() saves current context)
    lwp_yield();

    // 4. Resumed (Unblocked): Execution resumes here when another thread calls lwp_exit()
    
    // The terminated thread is now pointed to by curr_lwp->exited
    term_lwp = curr_lwp->exited;
    if (!term_lwp) return NO_THREAD; // Should not happen if unblocked correctly
    
    // Fall through to reaping logic
reap_thread:
    // 5. Reap the thread
    if (status) {
        *status = term_lwp->status;
    }
    
    // Deallocate resources
    if (term_lwp->stack) {
        munmap(term_lwp->stack, term_lwp->stacksize);
    }
    
    // Remove from all_lwps list 
    if (term_lwp == all_lwps) {// Case 1: Thread to be reaped is the head of the master list
        all_lwps = term_lwp->lib_one; 
    } else {  // Case 2: Thread is in the middle or tail
        thread curr = all_lwps;
        while (curr && curr->lib_one != term_lwp) {
            curr = curr->lib_one;
        }
        if (curr) {
            // Unlink term_lwp by setting the previous node's link to bypass it
            curr->lib_one = term_lwp->lib_one; 
        }
        // No else needed, if curr is null, it wasn't part of the list, which shuold never happen
    }
    tid_t reaped_tid = term_lwp->tid;
    free(term_lwp);
    
    return reaped_tid;
}

extern void lwp_set_scheduler(scheduler fun){
    scheduler old_sched = curr_sched;
    thread victim;

    // Handle default: if fun is NULL, set it to the default round-robin scheduler 
    if (fun == NULL) {
        fun = RoundRobin;
    }

    // Shutdown old scheduler if it is not the new one and has a shutdown function
    if (old_sched != NULL && old_sched != fun && old_sched->shutdown) {
        old_sched->shutdown();
    }
    
    // Transfer threads:
    if (old_sched != NULL && old_sched != fun) {
        // Transfer all runnable threads from old scheduler to the new one
        while ((victim = old_sched->next()) != NULL) {
            old_sched->remove(victim);
            fun->admit(victim);
        }
    }
    
    // Initialize new scheduler: call new_sched->init() if not null
    if (fun != NULL && fun->init) {
        fun->init();
    }

    // Update global: curr_sched = fun
    curr_sched = fun;
}

// returns value of the curr_sched pointer
extern scheduler lwp_get_scheduler(void){
    return curr_sched;
}

// searches all_lwps to find and return the thread 
// context matching the given tid, returns NULL if not found
extern thread tid2thread(tid_t tid){
    thread curr = all_lwps;
    while (curr) {
        if (curr->tid == tid) {
            return curr;
        }
        curr = curr->lib_one; // Assumes lib_one is used for the all_lwps list
    }
    return NULL;
}