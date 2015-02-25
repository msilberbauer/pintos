#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "filesys/file.c"




/* System calls that return a value can do so by modifying the "eax" member of struct intr_frame. */

static void syscall_handler (struct intr_frame *);

void syscall_init (void)
{
    intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void syscall_handler (struct intr_frame *f UNUSED)
{
    /* The stack pointer points to the systemcallnumber */    
    int syscallnr = *(f->esp); 
    switch(syscallnr)
    {
       case SYS_HALT :
           // Do something
           break;
       case SYS_EXIT :
           // Do something
           break;
       case SYS_EXEC :
           // Do something
           break;
       case SYS_WAIT :
           // Do something
           break;
       case SYS_CREATE :
           // Do something
           break;
       case SYS_REMOVE :
           // Do something
           break;
       case SYS_OPEN :
           const char *file = *(f->esp+1);
           return open(file);
           break;
       case SYS_FILSIZE :
           // Do something
           break;
       case SYS_READ :
           // Do something
           break;
       case SYS_WRITE :
           int fd = *(f->esp+1);
           *buffer = *(f->esp+2);
           unsigned size = *(f->esp+3);
           f->eax = write(fd, buffer, size);
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


    
            /*
    char *hey = "heeeeey\n\0";
    putbuf(hey,6);
    
    
    printf ("system call!\n");
    thread_exit ();
            */
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
    
    return -1; // No such file or maximum number of files open
    
    
        
    // put ind i første ledige plads i file descriptor table.
    // returner pladsen (fd)
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
    }else
    {
        struct file *f = thread_current()->fdtable[fd];

        /* Returns number of bytes written */
        return file_write (f, buffer, size);
    }
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

    // uint8_t *buf = (uint8_t) *buffer;
    if(fd == STDIN_FILENO) // fd = 0
    {
        // unsigned i;
        //for(i = 0; i < size; i++)
        // {
        //     buf[i] = input_getc();
        // }

        return size;
    }else
    {
        struct file *f = thread_current()->fdtable[fd];

        /* Returns number of bytes written */
        return file_read (f, buffer, size);
    }
}
