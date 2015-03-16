#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);

struct lock filesys_lock; /* Should not be neccesary once filesystem project
                             is implemented. But needed now for tests to succeed */

#endif /* userprog/syscall.h */
