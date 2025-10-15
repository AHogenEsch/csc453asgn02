#include "../include/lwp.h"

tid_t next_tid = 1;
thread (*curr_lwp)(void);
thread (*all_lwps)(void); // pointer to head of linked list
thread (*terminated)(void);
thread (*waiting)(void);
scheduler (*curr_sched)(void);


thread rr_head = NULL; // Head of the circular linked list

void rr_init(void) { /* Initialize rr_head to NULL */ }
void rr_shutdown(void) { /* Clean up list structure if needed */ }

void rr_admit(thread new) {
    /* Add 'new' to the end of the circular list */
}

void rr_remove(thread victim) {
    /* Remove 'victim' from the circular list */
}

thread rr_next(void) {
    /* Get the thread from the head, move it to the tail, and return it.
       If the list is empty, return NULL. */
}

int rr_qlen(void) {
    /* Traverse the list to count and return the number of threads. */
}


struct scheduler rr_publish = {
    rr_init, rr_shutdown, rr_admit, rr_remove, rr_next, rr_qlen
};


scheduler RoundRobin = &rr_publish; // The global pointer to the scheduler


// returns NO_THREAD on failure 
extern tid_t lwp_create(lwpfun,void *){
    //malloc to allocate memory for threadinfo, check for failure

    //allocate stack by checking rlimit stack soft limit 
    // getrlimit() deafult to 8MB

    // roudn up to the nearest multiple of page size (_SG_PAGE_SIZE)


    // use mmap() to allocate the stack memory. check for failure, free context and return no thread

    //initialize context
    // thread->state.fxsave = FPU_INIT;
    // stack setup: configure the stack to simulate a call to lwp_wrap()
    // determine the high address of the stack, align stack pointer rsp to a 16 byte boundary
    // push address of lwp_exit() onto the stack, which sevres as return address for lwp_wrap

    //thread->state.rsp = final aligned stack pointer
    //thread->state.rbp = initial stack frame

    //argument setup: lwp_wrap(func, arg) expects func in %rdi and arg in %rsi
    //thread ->state.rdi = (unsigned long) func
    //thread->state.rsi = (unsigned long) arg
    //thread->state.rax (instruction pointer RIP) = address of lwp_wrap()

    //finalize
    // thread->tid = next_tid++
    //thread->status = LWP_LIVE
    // add to all_lwps
    // call curr_sched->admit(thread)
    //return thread->tid
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
    //select next: call next_lwp = curr_sched->next()
    // if next_lwp is NULL
        // if the current thread is the original system thread (ie curr_lwp->stack == NULL), \
            then its a normal exit, call exit with the termination staus (low 8 bits of curr_lwp->status)
    //context switch: call swap_rfiles(&curr_lwp->state, &next_lwp->state) to save current context and load the next
    //update global: set curr_lwp = next_lwp
    //function returns into the next_lwp context

}
extern void  lwp_start(void){
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