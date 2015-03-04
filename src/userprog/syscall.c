#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
//#include "filesys/file.h"
//#include "filesys/file.c"




/* System calls that return a value can do so by modifying the "eax" member of struct intr_frame. */

struct lock filesys_lock;

static void syscall_handler (struct intr_frame *);
int open (const char *f);
int write(int fd, const void *buffer, unsigned size);
int filesize(int fd);
int read (int fd, void *buffer, unsigned size);
void get_arguments(struct intr_frame *f, int *arguments, int n);
int usr_to_kernel_ptr(const void *vaddr);
void is_valid_ptr(const void *vaddr);
void exit(int status);
bool create(const char *file, unsigned initial_size);
void close(int fd);
void valid_fd(int fd);
int exec(const char *cmd_line);

void syscall_init (void)
{
    lock_init(&filesys_lock);
    intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void syscall_handler (struct intr_frame *f UNUSED)
{    
    int arguments[3];

    is_valid_ptr((const void *)f->esp);

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
           /* TODO close alle filer */
           exit(arguments[0]);
           break;
       case SYS_EXEC :
           get_arguments(f,arguments,1);
           arguments[0] = usr_to_kernel_ptr((const char *) arguments[0]);
           f->eax = exec((const char *) arguments[0]);
           break;
       case SYS_WAIT :
           get_arguments(f,arguments,1);
           f->eax = process_wait((tid_t) arguments[0]);
           break;
       case SYS_CREATE :
           get_arguments(f,arguments,2);
           arguments[0] = usr_to_kernel_ptr((const char *) arguments[0]);
           f->eax = create((const char *) arguments[0],(unsigned) arguments[1]);
           break;
       case SYS_REMOVE :
           // Do something
           break;
       case SYS_OPEN :
           get_arguments(f,arguments,1);
           arguments[0] = usr_to_kernel_ptr((const char *) arguments[0]);
           f->eax = open(arguments[0]);
           break;
       case SYS_FILESIZE :
           get_arguments(f,arguments,1);
           f->eax = filesize(arguments[0]);
           break;
       case SYS_READ :
           get_arguments(f,arguments,3);
           arguments[1] = usr_to_kernel_ptr((void *) arguments[1]);
           f->eax = read(arguments[0], (void*) arguments[1], (unsigned)arguments[2]);
           break;
       case SYS_WRITE :
           get_arguments(f,arguments,3);
           arguments[1] = usr_to_kernel_ptr((const void *) arguments[1]);
           f->eax = write(arguments[0], (const void *) arguments[1], (unsigned)arguments[2]);
           break;
       case SYS_SEEK :
           // Do something
           break;
       case SYS_TELL :
           // Do something
           break;
       case SYS_CLOSE :
           get_arguments(f,arguments,1);
           close(arguments[0]);
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
    valid_fd(fd);
    /* Maybe do more checks on arguments */

    /*
      might cause problems if it is possible to change the meaning of
      fd 0 and fd 1. fx writing to files instead of console etc.
     */  
        
    if(fd == STDIN_FILENO) /* Can't write to stdin */
    {
        return -1;
    }
    
    if(fd == STDOUT_FILENO) // fd = 1
    {
        putbuf(buffer,size);
        return size;
    }else
    {
        lock_acquire(&filesys_lock);
        struct file *f = thread_current()->fdtable[fd];

        if(get_deny_write(f))
        {
            return 0;
        }

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
    valid_fd(fd);
    
    /* Maybe do more checks on arguments */

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
    }

    if(fd == STDOUT_FILENO) /* Can't read from stdout */
    {
        return -1;
    }
    else
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
        is_valid_ptr(ptr);
        arguments[i] = *ptr;
    }
    
}

int usr_to_kernel_ptr(const void *vaddr)
{
    
    is_valid_ptr(vaddr);

    void *ptr = pagedir_get_page (thread_current()->pagedir, vaddr);

    if(ptr == NULL) /* If the user address is unmapped */
    {
        exit(-1);
    }else
    {
        return (int) ptr;
    }
}

void is_valid_ptr(const void *vaddr)
{
    /* Terminate if process passed an invalid address */
    if(!is_user_vaddr(vaddr) || vaddr < 0x08048000) /* Bottom of user program */
    {
        exit(-1);
    }
}

void exit(int status)
{
    struct thread *cur = thread_current();
    cur->p->status = status;
    printf("%s: exit(%d)\n", cur->name, cur->p->status);
    thread_exit();
}


bool create(const char *file, unsigned initial_size)
{
    if(file == NULL)
    {
        exit(-1);
        return false; /* never reached */
    }
    else
    {
        return filesys_create(file,initial_size);
    }
}

void close(int fd)
{
    valid_fd(fd);

    if(thread_current()->fdtable[fd] != NULL)
    {
        file_close(thread_current()->fdtable[fd]);
        thread_current()->fdtable[fd] = NULL;
    }   
    
}

int filesize(int fd)
{
    valid_fd(fd);
    
    if(thread_current()->fdtable[fd] != NULL)
    {
        return file_length(thread_current()->fdtable[fd]);
    }

    return -1;
}


void valid_fd(int fd)
{
    if(fd > 9) /* Max allowed files opened */
    {
        exit(-1);
    }
}


int exec(const char *cmd_line)
{
    
    /* The parent should wait until it knows what happened to the child,
       whether it successfully loaded its executable or failed */

    int pid = process_execute(cmd_line);
    
    struct process *p = get_child(pid);

    //printf("WAALLLAAAAd\n");
    //printf("PID IS: %d \n", pid);
    /* Wait for the child to have been properly loaded */
    sema_down(&p->load);
    //printf("HEHEHEHEHEHEHEHEHHHE\n");
    
    /* At this point the child process has finished loading and it either failed
       or succeeded. The child could have received cpu cycles and could even have finished */

    
    if(p->loaded)
    {
        //printf("EXEC RETURNED: %d\n", pid);
        return pid;
    }else
    {
        return -1;
    }
    
}
