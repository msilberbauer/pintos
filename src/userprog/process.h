#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/synch.h"


struct process
{
    tid_t pid;               /* The process id */
    int status;              /* The exit status of the process */
    bool loaded;             /* Whether the child successfully loaded its executable */
    tid_t parent_id;         /* The id of the parent */
    struct semaphore wait;   /* A semaphore used when a parent wants
                                to wait for its child process */
    struct semaphore load;   /* A semaphore used when a parent wants
                                to wait until its child is loaded */
    struct list_elem elem;   /* List element used for a thread to keep track
                                of its child processes */
};

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

#endif /* userprog/process.h */
