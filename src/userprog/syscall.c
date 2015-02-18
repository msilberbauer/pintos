#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"




/* System calls that return a value can do so by modifying the "eax" member of struct intr_frame. */

static void syscall_handler (struct intr_frame *);

void syscall_init (void)
{
    intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void syscall_handler (struct intr_frame *f UNUSED)
{

    char *hey = "heeeeey\n\0";
    putbuf(hey,6);
    
    
    printf ("system call!\n");
    thread_exit ();
}



