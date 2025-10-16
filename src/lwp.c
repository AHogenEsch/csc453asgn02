#include "../include/lwp.h"

tid_t next_tid = 1;

#define waiting      (*(thread *)waiting)
#define terminated      (*(thread *)terminated)
#define curr_lwp      (*(thread *)curr_lwp)
#define all_lwps      (*(thread *)all_lwps)
#define curr_sched    (*(scheduler *)curr_sched)


thread rr_head = NULL; // Head of the circular linked list
int rr_cnt = 0;

void rr_init(void) { 
    rr_head = NULL;
    rr_cnt = 0;
 }

 /* Add 'new' to the end of the circular list */
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

/* Remove 'victim' from the circular list */
void rr_remove(thread victim) {
    if(rr_head->tid == victim->tid){
        // if the victim is the head of the list, remove it by changing \
           the next and previous pointers
        thread next = rr_head->sched_one;
        thread prev = rr_head->sched_two;
        prev->sched_one = next;
        next->sched_two = prev;
        rr_head = next;
    }
    else{
        thread curr = rr_head->sched_one;
        while(curr && (curr->tid != victim->tid) && curr->sched_one){
            // could also increment a counter and compare with qlen
            if (curr->tid == rr_head->tid){
                //if this is true, we've looped back around \
                    to the beginning of the list without finding victim
                    printf("Could not find victim in list\n");
                    return;
            }
            // continue looping
            curr = curr->sched_one;
        }
    // either curr is null, curr->sched_one is null, or we found our victim!
        if(!curr || !curr->sched_one){
            // in a circular list there should never be a null in the loop.
            printf("Attempted to remove a thread but loop found null\n");
            return;
        }
        else if(curr->tid == victim->tid){
            //victim found, remove it
            thread next = curr->sched_one;
            thread prev = curr->sched_two;
            next->sched_two = prev;
            prev->sched_one = next;
            printf("Successfully removed a victim\n");
            rr_cnt--;
        }
    }
}

/* Get the thread from the head, move it to the tail, and return it.
       If the list is empty, return NULL. */
thread rr_next(void) {
    thread next = rr_head;
    if(rr_head && rr_head->sched_one){
        // rr_head just stores the pointer to the beginning of the list \
        we just have to make it point to the next in line, the prev & next \
        pointers shuold still point to the same threads in the circular queue
        rr_head = rr_head->sched_one;
        // if there is 1 element, then rr_head->sched_one should be rr_head
    }
    if(!rr_head->sched_one){
        printf("rr_next: rr_head->sched_one is NULL\n");
    }
    // if rr_head was NULL, this will return NULL. 
    return next;

    
}
// assumes list only has runnable threads, and that \
threads are removed when terminated, decrementing rr_cnt
int rr_qlen(void) {
    return rr_cnt;
}

// uses sched_one as 'next' and sched_two as 'prev'
scheduler RoundRobin = &((struct scheduler) {
    rr_init, NULL, rr_admit, rr_remove, rr_next, rr_qlen
});


// returns NO_THREAD on failure 
extern tid_t lwp_create(lwpfun function, void *argument) {
    thread new_lwp;
    size_t stack_size;
    unsigned long *stack_base;
    unsigned long *tos; // Top of stack pointer for setup

    // 1. Allocate Context
    new_lwp = (thread)malloc(sizeof(struct threadinfo_st));
    if (!new_lwp) {
        // Malloc failed
        perror("malloc");
        return NO_THREAD;
    }
    // Zero out the entire structure for safety
    memset(new_lwp, 0, sizeof(struct threadinfo_st));

    // 2. Determine Stack Size (Max 8MB or soft limit, page-aligned)
    struct rlimit rl;
    if (getrlimit(RLIMIT_STACK, &rl) == 0 && rl.rlim_cur != RLIM_INFINITY) {
        stack_size = (size_t)rl.rlim_cur;
    } else {
        stack_size = DEFAULT_STACK_SIZE; 
    }

    // Round up stack size to the nearest multiple of page size
    long page_size = sysconf(_SC_PAGE_SIZE);
    if (page_size == -1) page_size = 4096; // Fallback value
    // Ensure stack_size is page-aligned (round up)
    stack_size = (stack_size + page_size - 1) & ~(page_size - 1);
    new_lwp->stacksize = stack_size;

    // 3. Allocate Stack (mmap)
    stack_base = (unsigned long *)mmap(NULL, stack_size, PROT_READ | PROT_WRITE, 
                                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (stack_base == MAP_FAILED) {
        free(new_lwp);
        return NO_THREAD;
    }
    new_lwp->stack = stack_base;

    // 4. Initialize Context (rfile and stack)

    // A. Initialize Registers
    // Set all registers to 0 initially. (Already done by memset)
    
    // Set FPU state
    new_lwp->state.fxsave = FPU_INIT;

    // B. Stack Setup (x86_64 ABI requires 16-byte alignment)
    // The stack grows from high address towards low address.
    // We set up the stack to hold the return sequence: lwp_wrap -> lwp_exit.
    
    // 1. Calculate the initial Top of Stack (TOS - high address of allocated memory)
    tos = (unsigned long *)((char *)new_lwp->stack + new_lwp->stacksize);

    // 2. Push lwp_exit() address (This is lwp_wrap's return address)
    tos--;
    *tos = (unsigned long)lwp_exit;
    
    // 3. Push a value for the Base Pointer (RBP) to establish a fake frame.
    // This value is not strictly required to be 17 but a consistent value is good practice.
    tos--;
    *tos = 17; 

    // 4. Align the current pointer to a 16-byte boundary.
    // This address will hold the address of lwp_wrap, which swap_rfiles's RET will jump to.
    unsigned long aligned_rsp = (unsigned long)tos;
    aligned_rsp = (aligned_rsp & ~0xFUL); // Align down to 16-byte boundary

    // 5. Place the lwp_wrap address at the aligned address.
    tos = (unsigned long *)aligned_rsp;
    *tos = (unsigned long)lwp_wrap; 

    // 6. Set the LWP's RSP and RBP
    new_lwp->state.rsp = (unsigned long)tos;
    new_lwp->state.rbp = (unsigned long)new_lwp->stack; // RBP is not critical for startup but can point to the base.

    // C. Argument Setup
    // lwp_wrap(lwpfun fun, void *arg): fun in RDI, arg in RSI
    new_lwp->state.rdi = (unsigned long)function;
    new_lwp->state.rsi = (unsigned long)argument;
    
    // 5. Finalize Context
    new_lwp->tid = next_tid++;
    new_lwp->status = LWP_LIVE;

    
    // 6. Link to Global All-LWP List (simple list for tid2thread)
    // Note: Assuming 'all_lwps' is a global pointer to the head, and using lib_one/two for list management.

    //lib_one points to next lwp, simple single linked list ending with lib_one = NULL for the last element in list
    new_lwp->lib_one = all_lwps;
    all_lwps = new_lwp;

    // Initialize list pointers (already done by memset, but redundant explicit setting is safe)
    new_lwp->sched_one = new_lwp->sched_two = new_lwp->exited = NULL;
    // 7. Admit to Scheduler
    // Set default scheduler if not initialized (though lwp_start is responsible for this)
    if (curr_sched == NULL) {
        curr_sched = RoundRobin;
        if (curr_sched->init) {
            curr_sched->init();
        }
    }
    curr_sched->admit(new_lwp);

    // 8. Return tid
    return new_lwp->tid;
}

//terminates calling lwp. This function does not return.
extern void  lwp_exit(int status){
    //set status: set curr_lwp->status using MKTERMSTAT(LWP_TERM, status)

    //handle waiters if waiting isnt empty
        //dequeue the oldest waiting thread, set waiting_thread->exited = curr_lwp (the recently terminated thread)
        //call curr_sched->admit(waiting_thread)
    // else waiting is empty
        // enqueue curr_lwp onto terminated

    //remove from sched: call curr_sched->remove(curr_lwp)

    //yield: call lwp_yield(). 
}
//returns the tid from the current LWP's context,\
     or NO_THREAD if curr_lwp is NULL
extern tid_t lwp_gettid(void){
    if(curr_lwp){
        return curr_lwp->tid;
    }
    else{
        return NO_THREAD;
    }

}
//yields control to the next LWP
extern void  lwp_yield(void){
    thread next_lwp;
    thread old_lwp;
    
    if (curr_sched == NULL || curr_lwp == NULL) {
        // Should only happen if called before lwp_start or after total failure
        exit(EXIT_FAILURE); 
    }
    
    // 1. Select the next LWP
    next_lwp = curr_sched->next();

    // 2. Handle System Termination (Deadlock/Empty Queue)
    if (next_lwp == NULL) {
        // No more runnable threads; terminate the process.
        // The exit status must be the low 8 bits of the current LWP's status.
        int term_status = LWPTERMSTAT(curr_lwp->status);
        
        // Before exiting, ensure the scheduler state is clean
        if (curr_sched->shutdown) {
            curr_sched->shutdown();
        }
        exit(term_status); 
    }

    // 3. Context Switch
    
    // Save the pointer to the LWP context being saved.
    old_lwp = curr_lwp;
    
    // Crucially, set the global current thread pointer to the new thread 
    // BEFORE the switch. When the new thread resumes, it needs this global 
    // pointer to be correct.
    curr_lwp = next_lwp; 

    // Atomically save the registers of the old thread and load the registers 
    // of the new thread.
    swap_rfiles(&old_lwp->state, &curr_lwp->state);
    
    // 4. Resumption
    // Execution resumes here for the thread whose context was SAVED.
    // If the thread was just yielding, it continues from this point.

    //select next: call next_lwp = curr_sched->next()
    // if next_lwp is NULL
        // if the current thread is the original system thread (ie curr_lwp->stack == NULL), \
            then its a normal exit, call exit with the termination staus (low 8 bits of curr_lwp->status)
    //context switch: call swap_rfiles(&curr_lwp->state, &next_lwp->state) to save current context and load the next
    //update global: set curr_lwp = next_lwp
    //function returns into the next_lwp context

}
extern void  lwp_start(void){
    thread original_thread;
    
    // The system should only be started once.
    if (curr_lwp != NULL) {
        return;
    }

    // allocate context for the calling thread, the original process
    original_thread = (thread)malloc(sizeof(struct threadinfo_st));
    if (!original_thread) {
        perror("malloc: Failed to allocate context for original thread");
        exit(EXIT_FAILURE);
    }
    // clear it
    memset(original_thread, 0, sizeof(struct threadinfo_st));

    // Initialize fields
    original_thread->tid = next_tid++;
    original_thread->status = LWP_LIVE;
    original_thread->stack = NULL; // Mark as original system stack
    
    // Set current LWP before saving its context
    curr_lwp = original_thread;
    
    // 2. Save the current context (registers) of the original thread.
    // This is a save-only call (new is NULL). Execution continues normally here.
    swap_rfiles(&curr_lwp->state, NULL);

    // 3. Set up scheduler (if not already done by lwp_create)
    if (curr_sched == NULL) {
        curr_sched = RoundRobin;
        if (curr_sched->init) {
            curr_sched->init();
        }
    }
    
    // 4. Admit the original thread to the scheduler's pool.
    curr_sched->admit(curr_lwp);

    // 5. Link to the global all-LWP list
    curr_lwp->lib_one = all_lwps;
    all_lwps = curr_lwp;
    
    // 6. Yield control. The first lwp_yield() call will select the first
    // user-created thread (if any exist) and switch context to it.
    lwp_yield();

    // Execution should never return here unless all other LWPs have yielded
    // or terminated, and the original LWP is the one being scheduled.

    //convert caller: create thread context for calling thread
    // allocate context, assign next tid, and set status to LWP_LIVE
    //set thread-> stack to NULL and mark as the original system stack, and prevent deallocation
    // save the current register state using swap_rfiles with \
        the second argument set to NULL (swap_rfiles(&thread->state, NULL))
    //admit: curr_sched->admit(thread)
    //switch: call lwp_yield to yield control to the first scheduled LWP
}

//waits for and reaps a terminated LWP
extern tid_t lwp_wait(int *status){
// check terminated list. If not empty:
    // dequee the oldest thread, term_lwp
    // if status not null, set *status to term_lwp->Status
    //deallocate resources: if term_lwp->stack isnt null (not original system thread) use munmap() to free stack
    // free thread context struct
    //return term_lwp->tid

    //check for deadlock: if curr_sched->qlen() <=1 (meaning the current thread is the only runnable one, so nothing else can terminate) return NO_THREAD

    //block:
    //curr_sched->remove(curr_lwp)
    // enqueue curr_lwp onto waiting
    // call lwp_yield

    //resumed: When the thread resumes (after lwp_exit() unblocks it), the exited pointer will point to the thread that terminated
    // set term_lwp = curr_lwp->exited
    //deallocate resources
    //return term_lwp->>tid


}
extern void  lwp_set_scheduler(scheduler fun){
    //shutdown old scheduler if it is not new and has a shutdown function

    //handle default: if fun is NULL, set it to the default round-robin scheduler 
    //transfer threads:
    // While the old scheduler's next() returns a thread, call 
    // old_Sched->remove(thread)
    // new_sched ->admit(thread)

    //initilaize new scheduler: call new_sched->init() if not null

    //update global: curr_sched = fun
}
//returns value of the curr_sched pointer
extern scheduler lwp_get_scheduler(void){
    return curr_sched;
}
// searches all_lwps to find and return the thread \
    context matching the given tid, returns NULL if not found
extern thread tid2thread(tid_t tid){
    
}