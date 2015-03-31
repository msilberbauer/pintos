#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "devices/timer.h"
#include "threads/fixedpoint.h"
#include "vm/frame.h"
#include "vm/page.h"
#include <hash.h>
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* List of all sleeping processes which are in THREAD_BLOCKED
   state */
static struct list sleeping_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame
{
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
};

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */
static int load_avg;            /* The average number of threads ready to run
                                   over the past minute saved in 17.14 format */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);
bool thread_order_function(const struct list_elem *a,const struct list_elem *b, void *aux);
bool sleep_order_function(const struct list_elem *a,const struct list_elem *b, void *aux);

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void thread_init (void)
{
    ASSERT (intr_get_level () == INTR_OFF);
    
    lock_init (&tid_lock);
    list_init (&ready_list);
    list_init (&all_list);
    list_init (&sleeping_list);
    
    load_avg = 0;

    /* Set up a thread structure for the running thread. */
    initial_thread = running_thread ();
    init_thread (initial_thread, "main", PRI_DEFAULT);
    initial_thread->status = THREAD_RUNNING;
    initial_thread->tid = allocate_tid ();

    if(thread_mlfqs)
    {
        /* The initial thread should have a start nice and priority value of 0 */
        initial_thread->nice = 0;
        initial_thread->recent_cpu = 0;
        calculate_priority(initial_thread, NULL);
    }        
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void thread_start (void)
{
    /* Create the idle thread. */
    struct semaphore idle_started;
    sema_init (&idle_started, 0);
    thread_create ("idle", PRI_MIN, idle, &idle_started);

    /* Start preemptive thread scheduling. */
    intr_enable ();

    /* Wait for the idle thread to initialize idle_thread. */
    sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void thread_tick (int64_t nowtick)
{
    struct thread *t = thread_current ();

    /* Update statistics. */
    if (t == idle_thread)
        idle_ticks++;
#ifdef USERPROG
    else if (t->pagedir != NULL)
        user_ticks++;
#endif
    else
        kernel_ticks++;

    
    if(thread_mlfqs)
    {
        /* At every timer interrupt recent cpu is incremented by 1 for the
           running thread */
        if(thread_current != idle_thread)
        {
            thread_current()->recent_cpu = addn(thread_current()->recent_cpu,1);
        }

        /* Recalculate all thread's priorities every 4 tick */
        if(timer_ticks() % 4 == 0)
        {
            update_all_priority();
        }

        /* Every second if TIMER_FREQ is 100, which it should be */
        if(timer_ticks() % TIMER_FREQ == 0)
        {
            update_all_recent_cpu();
            update_load_avg();
        }
    }
        
    /* Check the sleeping_list and wake up threads with ending sleeptimer */
    struct list_elem *e;

    for (e = list_begin (&sleeping_list); e != list_end (&sleeping_list);
           e = list_next (e))
    {
        struct thread *p = list_entry (e, struct thread, sleepelem);

        if(nowtick >= p->wake_tick)
        {
            /* Remove from sleeping list */
            list_pop_front(&sleeping_list);

            p->wake_tick = 0;
            thread_unblock(p);            
        }else
        {
            /* There can be no more threads to be woken up
               because the sleeping list is ordered */
            break;
        }
    }

    /* Enforce preemption. */
    if (++thread_ticks >= TIME_SLICE)
    {
        intr_yield_on_return();
    }
}

/* Prints thread statistics. */
void thread_print_stats (void)
{
    printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
            idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t thread_create (const char *name, int priority, thread_func *function, void *aux)
{
    struct thread *t;
    struct kernel_thread_frame *kf;
    struct switch_entry_frame *ef;
    struct switch_threads_frame *sf;
    tid_t tid;

    ASSERT (function != NULL);

    /* Allocate thread. */
    t = palloc_get_page (PAL_ZERO);
    if (t == NULL)
        return TID_ERROR;

    /* Initialize thread. */
    init_thread (t, name, priority);
    
    tid = t->tid = allocate_tid ();

    enum intr_level old_level = intr_disable ();
    
    /* Stack frame for kernel_thread(). */
    kf = alloc_frame (t, sizeof *kf);
    kf->eip = NULL;
    kf->function = function;
    kf->aux = aux;

    /* Stack frame for switch_entry(). */
    ef = alloc_frame (t, sizeof *ef);
    ef->eip = (void (*) (void)) kernel_thread;

    /* Stack frame for switch_threads(). */
    sf = alloc_frame (t, sizeof *sf);
    sf->eip = switch_entry;
    sf->ebp = 0;

    intr_set_level (old_level);

    struct process *p = malloc(sizeof(struct process));
    p->pid = t->tid;
    p->status = false;
    p->parent_id = thread_current()->tid;
    sema_init(&p->wait, 0);
    sema_init(&p->load, 0);
    list_push_back (&thread_current()->children, &p->elem);
    t->p = p;
    
    /* Add to run queue. */
    thread_unblock (t);

    /* If the thread just created has a higher priority
       than the current running thread, the current running thread
       yields */
    if(t->priority > thread_current()->priority)
    {
        thread_yield();
    }
    
    return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void thread_block (void)
{
    ASSERT (!intr_context ());
    ASSERT (intr_get_level () == INTR_OFF);

    thread_current ()->status = THREAD_BLOCKED;
    schedule ();
}

/* An order function for the ready list which orders the threads
   with the highest priority first and the lowest priority last*/
bool thread_order_function(const struct list_elem *a,
                           const struct list_elem *b,
                           void *aux UNUSED)
{
    struct thread *t1 = list_entry (a, struct thread, elem);
    struct thread *t2 = list_entry (b, struct thread, elem);

    return t1->priority > t2->priority;
}


/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void thread_unblock (struct thread *t)
{
    enum intr_level old_level;

    ASSERT (is_thread (t));

    old_level = intr_disable ();
    ASSERT (t->status == THREAD_BLOCKED);
    list_insert_ordered(&ready_list, &t->elem, thread_order_function, NULL);
    t->status = THREAD_READY;    
    intr_set_level (old_level);    
}

/* Returns the name of the running thread. */
const char *thread_name (void)
{
    return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *thread_current (void)
{
    struct thread *t = running_thread ();

    /* Make sure T is really a thread.
       If either of these assertions fire, then your thread may
       have overflowed its stack.  Each thread has less than 4 kB
       of stack, so a few big automatic arrays or moderate
       recursion can cause stack overflow. */
    ASSERT (is_thread (t));
    ASSERT (t->status == THREAD_RUNNING);

    return t;
}

/* Returns the running thread's tid. */
tid_t thread_tid (void)
{
    return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void thread_exit (void)
{
    ASSERT (!intr_context ());

#ifdef USERPROG
    process_exit ();
#endif

    /* Remove thread from all threads list, set our status to dying,
       and schedule another process.  That process will destroy us
       when it calls thread_schedule_tail(). */
    intr_disable ();
    list_remove (&thread_current()->allelem);
    thread_current ()->status = THREAD_DYING;
    schedule ();
    NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void thread_yield (void)
{
    struct thread *cur = thread_current ();
    enum intr_level old_level;

    ASSERT (!intr_context ());

    old_level = intr_disable ();
    if (cur != idle_thread)
    {
        list_insert_ordered(&ready_list, &cur->elem, thread_order_function, NULL);
    }
    
    cur->status = THREAD_READY;
    schedule ();
    intr_set_level (old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void thread_foreach (thread_action_func *func, void *aux)
{
    struct list_elem *e;

    ASSERT (intr_get_level () == INTR_OFF);

    for (e = list_begin (&all_list); e != list_end (&all_list);
            e = list_next (e))
    {
        struct thread *t = list_entry (e, struct thread, allelem);
        func (t, aux);
    }
}


/* Donate *t a new priority. */
void donate(struct thread *t, int new_priority)
{    
    if(thread_mlfqs)
    {
        return;
    }
    
    int old = t->priority;
    if(new_priority < PRI_MIN || new_priority > PRI_MAX)
    {
        t->priority = PRI_DEFAULT;
    }else
    {
        t->priority = new_priority;
    }
    
    /* If the current running thread lowered its priority
       we check whether there is a thread on the ready list with
       a higher priority, if so the current running thread yields */
    if(t == thread_current())
    {
        if(!list_empty(&ready_list) && old > new_priority)
        {
            struct list_elem *front = list_front (&ready_list);
            struct thread *t = list_entry (front, struct thread, elem);
            if(new_priority < t->priority)
            {
                thread_yield();
            }
        }
    }else
    {
        /* A thread on the ready queue has had its priority changed.
           We must ensure that the ready list is still ordered. */
        list_remove(&t->elem);
        list_insert_ordered(&ready_list, &t->elem, thread_order_function, NULL);

        /* Yield the current thread if the changed thread on the ready
           queue now has a higher priority*/
        if(t->priority > thread_current()->priority)
        {
            thread_yield();
        }
    }    
}


/* Sets the current thread's priority to NEW_PRIORITY. */
void thread_set_priority (int new_priority)
{
    thread_other_set_priority (thread_current(), new_priority);
}

/* Sets t's priority to new_priority. */
void thread_other_set_priority (struct thread *t, int new_priority)
{
    /* When the mlfqs is enabled thread no longer directly control
       their own priorities */
    if(thread_mlfqs)
    {
        return;
    }
    
    /* If the current running thread is holding one or more locks
       then it could have been donated a higher priority. It is
       not allowed to lower its priority below the highest donated priority.
       The effect should take place */
    if(!list_empty(&t->locks)) 
    {
        list_sort(&t->locks, lock_order_function, NULL);
        struct lock *l = list_entry (list_front(&t->locks), struct lock, lockelem);

        if(l->priority > new_priority)
        {
            t->original_priority = new_priority;
            return;
        }
    }  

        
    int old = t->priority;
    if(new_priority < PRI_MIN || new_priority > PRI_MAX)
    {
        t->priority = PRI_DEFAULT;
        t->original_priority = PRI_DEFAULT;
    }else
    {
        t->priority = new_priority;
        t->original_priority = new_priority;
    }
    
    /* If the current running thread lowered its priority
       we check whether there is a thread on the ready list with
       a higher priority, if so the current running thread yields */
    if(t == thread_current())
    {
        if(!list_empty(&ready_list) && old > new_priority)
        {
            struct list_elem *front = list_front (&ready_list);
            struct thread *t = list_entry (front, struct thread, elem);
            if(new_priority < t->priority)
            {
                thread_yield();
            }
        }
    }else
    {
        /* A thread on the ready queue has had its priority changed.
           We must ensure that the ready list is still ordered. */
        list_remove(&t->elem);
        list_insert_ordered(&ready_list, &t->elem, thread_order_function, NULL);

        /* Yield the current thread if the changed thread on the ready
           queue now has a higher priority*/
        if(t->priority > thread_current()->priority)
        {
            thread_yield();
        }
    }    
}

/* Returns the current thread's priority. */
int thread_get_priority (void)
{
    if(thread_mlfqs)
    {
        return;
    }
    
    return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
void thread_set_nice (int nice UNUSED)
{
    /* Set the current nice value to the new nice value */    
    int nicee = nice;
    if(nicee > 20)
    {
        nicee = 20;
    }
    if(nicee < -20)
    {
        nicee = -20;
    }

    thread_current()->nice = nice;
    
    /* Recalculate the thread's priority based on the new nice value */
    calculate_priority(thread_current(), NULL);
    
    /* If the running thread no longer has the highest priority it yields */
    // TODO make this a function somewhere, remove code duplication.
    if(!list_empty(&ready_list))
    {
        struct list_elem *front = list_front (&ready_list);
        struct thread *t = list_entry (front, struct thread, elem);
        if(thread_current()->priority < t->priority)
        {
            thread_yield();
        }
    }
}


/* Calculates the priority for a thread. */
void calculate_priority(struct thread *t, void *aux)
{
    /* t->priority = PRI_MAX - (t->recent_cpu/4) - (t->nice*2); */

    int result = convert_to_fixedpoint(PRI_MAX) - (t->recent_cpu/4) - convert_to_fixedpoint(t->nice*2);
    t->priority = convert_to_int_round_zero(result);
}

/* Updates all thread's priority in the system */
void update_all_priority(void)
{
    thread_foreach(calculate_priority, NULL);    
}


/* Calculates the recent cpu for a thread i.e. how much CPU time the
   thread has received recently */
void calculate_recent_cpu(struct thread *t, void *aux)
{
    /* recent_cpu = (2*load_avg)/(2*load_avg + 1) * recent_cpu + nice */
    
    int a = 2 * load_avg;
    int b = convert_to_fixedpoint(1);
    int c = a + b;    
        
    t->recent_cpu = multiply(divide(a,c),t->recent_cpu) + convert_to_fixedpoint(t->nice);  
}

/* Updates all thread's recent_cpu in the system */
void update_all_recent_cpu (void)
{
    thread_foreach(calculate_recent_cpu, NULL);    
}

/* Updates the systems global load average. */
void update_load_avg(void)
{    
    /* load_avg = (59/60) * load_avg + (1/60) * ready_threads */
    
    int num_ready_threads = list_size(&ready_list);

    /* Ready threads is the number of threads that are either ready or running
       not including the idle thread. So we have to count the running thread */
    if(thread_current() != idle_thread)
    {
       num_ready_threads = num_ready_threads + 1;
    }
    
    int a = convert_to_fixedpoint(59);
    int b = convert_to_fixedpoint(60);
    int c = convert_to_fixedpoint(1);

    load_avg = multiply(divide(a,b),load_avg) + divide(c,b) * num_ready_threads;
    
}  

/* Returns the current thread's nice value. */
int thread_get_nice (void)
{
    return thread_current()->nice;
}

/* Returns 100 times the system load average. */
int thread_get_load_avg (void)
{
    return convert_to_int_round_nearest(load_avg * 100);
}

/* Returns 100 times the current thread's recent_cpu value. */
int thread_get_recent_cpu (void)
{
    return convert_to_int_round_nearest(thread_current()->recent_cpu * 100);
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void idle (void *idle_started_ UNUSED)
{
    struct semaphore *idle_started = idle_started_;
    idle_thread = thread_current ();
    sema_up (idle_started);

    for (;;)
    {
        /* Let someone else run. */
        intr_disable ();
        thread_block ();

        /* Re-enable interrupts and wait for the next one.

           The `sti' instruction disables interrupts until the
           completion of the next instruction, so these two
           instructions are executed atomically.  This atomicity is
           important; otherwise, an interrupt could be handled
           between re-enabling interrupts and waiting for the next
           one to occur, wasting as much as one clock tick worth of
           time.

           See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
           7.11.1 "HLT Instruction". */
        asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void kernel_thread (thread_func *function, void *aux)
{
    ASSERT (function != NULL);

    intr_enable ();       /* The scheduler runs with interrupts off. */
    function (aux);       /* Execute the thread function. */
    thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *running_thread (void)
{
    uint32_t *esp;

    /* Copy the CPU's stack pointer into `esp', and then round that
       down to the start of a page.  Because `struct thread' is
       always at the beginning of a page and the stack pointer is
       somewhere in the middle, this locates the curent thread. */
    asm ("mov %%esp, %0" : "=g" (esp));
    return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool is_thread (struct thread *t)
{
    return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void init_thread (struct thread *t, const char *name, int priority)
{
    enum intr_level old_level;
    
    
    ASSERT (t != NULL);
    ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
    ASSERT (name != NULL);

    memset (t, 0, sizeof *t);
    t->status = THREAD_BLOCKED;
    strlcpy (t->name, name, sizeof t->name);
    t->stack = (uint8_t *) t + PGSIZE;

    if(thread_mlfqs)
    {
        if(t != initial_thread && t != idle_thread)
        {            
            /* Inherit from parent*/
            t->nice = thread_current()->nice;
            t->recent_cpu = thread_current()->recent_cpu;

            /* Calculate its priority */
            calculate_priority(t, NULL);
        }        
    }else
    {
        t->priority = priority;
    }
    
    t->original_priority = priority;
    list_init (&t->locks);
    list_init (&t->mmaps);
    t->desiring_lock = NULL;
    t->wake_tick = 0;

    t->magic = THREAD_MAGIC;
    
    list_init (&t->children);

    old_level = intr_disable ();
    list_push_back (&all_list, &t->allelem);
    intr_set_level (old_level);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *alloc_frame (struct thread *t, size_t size)
{
    /* Stack data is always allocated in word-size units. */
    ASSERT (is_thread (t));
    ASSERT (size % sizeof (uint32_t) == 0);

    t->stack -= size;
    return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *next_thread_to_run (void)
{
    if (list_empty (&ready_list))
    {
        return idle_thread;
    }
    else
    {
        /* The front thread on the ready list is the one with the
           highest priority */
        return list_entry (list_pop_front (&ready_list), struct thread, elem);
    }
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void thread_schedule_tail (struct thread *prev)
{
    struct thread *cur = running_thread ();

    ASSERT (intr_get_level () == INTR_OFF);

    /* Mark us as running. */
    cur->status = THREAD_RUNNING;

    /* Start new time slice. */
    thread_ticks = 0;

#ifdef USERPROG
    /* Activate the new address space. */
    process_activate ();
#endif

    /* If the thread we switched from is dying, destroy its struct
       thread.  This must happen late so that thread_exit() doesn't
       pull out the rug under itself.  (We don't free
       initial_thread because its memory was not obtained via
       palloc().) */
    if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread)
    {
        ASSERT (prev != cur);
        palloc_free_page (prev);
    }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void schedule (void)
{
    struct thread *cur = running_thread ();
    struct thread *next = next_thread_to_run ();
    struct thread *prev = NULL;

    ASSERT (intr_get_level () == INTR_OFF);
    ASSERT (cur->status != THREAD_RUNNING);
    ASSERT (is_thread (next));

    if (cur != next)
        prev = switch_threads (cur, next);
    thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
static tid_t allocate_tid (void)
{
    static tid_t next_tid = 1;
    tid_t tid;

    lock_acquire (&tid_lock);
    tid = next_tid++;
    lock_release (&tid_lock);

    return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);

/* An order function for the sleeping list which orders the threads
   with the highest wake_tick last and the lowest wake_tick first */
bool sleep_order_function(const struct list_elem *a,
                          const struct list_elem *b,
                          void *aux UNUSED)
{
    struct thread *t1 = list_entry (a, struct thread, sleepelem);
    struct thread *t2 = list_entry (b, struct thread, sleepelem);

    return t1->wake_tick < t2->wake_tick;
}

/* Sleep the current thread */
void thread_sleep(int64_t wake_tick)
{
    /* Put the current thread on the sleeplist and block it. */
    enum intr_level old = intr_disable();
    
    struct thread *t = thread_current ();
    t->wake_tick = wake_tick;
    list_insert_ordered(&sleeping_list, &t->sleepelem, sleep_order_function, NULL);
    
    thread_block();
    intr_set_level(old);
}
