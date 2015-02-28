#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
//#include "filesys/file.h"
//#include "filesys/filesys.h"




/* System calls that return a value can do so by modifying the "eax" member of struct intr_frame. */

struct lock filesys_lock;

static void syscall_handler (struct intr_frame *);
int open (const char *f);
int write(int fd, const void *buffer, unsigned size);
int read (int fd, void *buffer, unsigned size);
void get_arguments(struct intr_frame *f, int *arguments, int n);
int usr_to_kernel_ptr(const void *vaddr);
void is_valid_ptr(const void *vaddr);
void exit(int status);

void syscall_init (void)
{
    lock_init(&filesys_lock);
    intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void syscall_handler (struct intr_frame *f UNUSED)
{    
    int arguments[3];

    //is_valid_ptr((const void *)f->esp);

    /* The stack pointer points to the systemcallnumber */    
    int syscallnr = *((int *)f->esp);
    //printf ("A system call: %d\n", syscallnr);
    switch(syscallnr)
    {
       case SYS_HALT :
           shutdown_power_off();
           break;
       case SYS_EXIT :
           get_arguments(f,arguments,1);
           exit(arguments[0]);
           break;
       case SYS_EXEC :
           // Do something
           break;
       case SYS_WAIT :
           get_arguments(f,arguments,1);
           f->eax = process_wait((tid_t) arguments[0]);
           break;
       case SYS_CREATE :
           // Do something
           break;
       case SYS_REMOVE :
           // Do something
           break;
       case SYS_OPEN :
           get_arguments(f,arguments,1);
           f->eax = open(arguments[0]);
           break;
       case SYS_FILESIZE :
           // Do something
           break;
       case SYS_READ :
           //get_arguments(f,arguments,3);
           //arguments[1] = usr_to_kernel_ptr((void *) arguments[1]);
           //f->eax = read(arguments[0], (void*) arguments[1], (unsigned)arguments[2]);
           break;
       case SYS_WRITE :
           get_arguments(f,arguments,3);
           arguments[1] = usr_to_kernel_ptr((const void *) arguments[1]);
           f->eax = write(arguments[0], (const void *) arguments[1], (unsigned)arguments[2]);
           //printf ("write done \n");
           break;
       case SYS_SEEK :
           // Do something
           break;
       case SYS_TELL :
           // Do something
           break;
       case SYS_CLOSE :
           // Do something
           break;
       
      
    }
}

int open (const char *file)
{
    /* Tænk over synchronization, og flere læse/read på samme fil */
    struct file *f = filesys_open(file);

    if(f != NULL)
    {
        int i;
        for(i = 2; i < 10; i++) // for now 0 and 1 reserved for stdin and stdout. not ideal.
        {
            if(thread_current()->fdtable[i] == NULL)
            {
                thread_current()->fdtable[i] = f;
                return i;
            }
        }
    }

    return -1;
}





/* Writes size bytes from buffer to the open file fd.
   Returns the number of bytes actually written, which may be less
   than size if some bytes could not be written.

   Writing past end-of-file would normally extend the file
   but file growth is not implemented by the basic file system.
   The expected behavior is to write as many bytes as possible up to
   end-of-file and return the actual number written, or 0 if no bytes
   could be written at all.

   Fd 1 writes to the console. Your code to write to the console should
   write all of buffer in one call to putbuf(), at least as long as
   size is not bigger than a few hundred bytes.
   (It is reasonable to break up larger buffers.) Otherwise, lines of
   text output by different processes may end up interleaved on the console
   confusing both human readers and our grading scripts. */
int write(int fd, const void *buffer, unsigned size)
{
    /* Maybe do checks on arguments */
    
    /*
      might cause problems if it is possible to change the meaning of
      fd 0 and fd 1. fx writing to files instead of console etc.
     */  
    if(fd == STDOUT_FILENO) // fd = 1
    {
        putbuf(buffer,size);
        return size;
    }else
    {
        lock_acquire(&filesys_lock);
        struct file *f = thread_current()->fdtable[fd];

        lock_release(&filesys_lock);
        /* Returns number of bytes written */
        return file_write (f, buffer, size);
    }
    lock_release(&filesys_lock);
    return -1;
}


/* Reads size bytes from the file open as fd into buffer.
   Returns the number of bytes actually read (0 at end of file)
   or -1 if the file could not be read (due to a condition other than end of file).
   Fd 0 reads from the keyboard using input_getc(). */
int read (int fd, void *buffer, unsigned size) 
{
    /* Maybe do checks on arguments */
    
    /*
      might cause problems if it is possible to change the meaning of
      fd 0 and fd 1. fx writing to files instead of console etc.
     */

    uint8_t *buf = (uint8_t *) buffer;
    if(fd == STDIN_FILENO) // fd = 0
    {
        unsigned i;
        for(i = 0; i < size; i++)
        {
            buf[i] = input_getc();
        }

        return size;
    }else
    {
        struct file *f = thread_current()->fdtable[fd];

        /* Returns number of bytes written */
        return file_read (f, buffer, size);
    }

    return -1;
}


void get_arguments(struct intr_frame *f, int *arguments, int n)
{
    int *ptr;
    int i;
    for (i = 0; i < n; i++)
    {
        ptr = (int *) f->esp+i+1;        
        arguments[i] = *ptr;

        // Maybe we should check for valid pointer?
    }
}

int usr_to_kernel_ptr(const void *vaddr)
{
    is_valid_ptr(vaddr);

    void *ptr = pagedir_get_page (thread_current()->pagedir, vaddr);

    if(ptr == NULL) /* If the user address is unmapped */
    {
        // Call our exit system call
    }else
    {
        return (int) ptr;
    }
}

void is_valid_ptr(const void *vaddr)
{
    /* Terminate if process passed an invalid */
    if(!is_user_vaddr(vaddr))
    {
        char *s = "Invalid pointer\n\0";
        putbuf(s,19);
        // We should call our own exit system call.
    }
}

void exit(int status)
{
    struct thread *cur = thread_current();
    cur->p->status = status;
    printf("%s: exit(%d)\n", cur->name, cur->p->status);
    thread_exit();
}
