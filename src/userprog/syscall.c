#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "threads/palloc.h"
#include "vm/page.h"
#include "vm/frame.h"
#include "userprog/exception.h"

#define MAX_OPEN_FILES 9

static void syscall_handler (struct intr_frame *);
int open (const char *file);
int write(int fd, const void *buffer, unsigned size);
int filesize(int fd);
int read (int fd, void *buffer, unsigned size, void * sp);
void exit(int status);
bool create(const char *file, unsigned initial_size);
void close(int fd);
void is_valid_fd(int fd);
int exec(const char *cmd_line);
void seek(int fd, unsigned position);
bool is_valid_ptr (const void *ptr);

void syscall_init (void)
{
    lock_init(&filesys_lock);
    intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* The system call handler. System calls that return a value can do so
   by modifying the "eax" member of struct intr_frame. */
static void syscall_handler (struct intr_frame *f UNUSED)
{  
    uint32_t *sp = f->esp;
    is_valid_ptr((const void *)f->esp);
    
    /* The stack pointer points to the systemcallnumber */    
    int syscallnr = *((int *)sp);
    switch(syscallnr)
    {
       case SYS_HALT :
           shutdown_power_off();
           break;
       case SYS_EXIT:
           if(is_valid_ptr(sp + 1))
           {               
               exit(*(sp + 1));
           }
           break;
       case SYS_EXEC :
           if(is_valid_ptr (sp + 1))
           {
               is_valid_ptr((char *) *(sp + 1));
               f->eax = exec((char *) *(sp + 1));
           }
           break;
       case SYS_WAIT:
           if(is_valid_ptr(sp + 1))
              f->eax = process_wait(*(sp + 1));
           break;
       case SYS_CREATE :
           if(is_valid_ptr(sp + 1) &&
              is_valid_ptr(sp + 2))
           {
               is_valid_ptr((char *) *(sp + 1));
               f->eax = create((char *) *(sp + 1), *(sp + 2));
           }
           break;
       case SYS_REMOVE :
           // TODO: Do something
           break;
       case SYS_OPEN :
           if(is_valid_ptr(sp + 1))
           {
               is_valid_ptr((const char *) *(sp + 1));
               f->eax = open((const char *) *(sp + 1));
           }               
           break;           
       case SYS_FILESIZE :
           if(is_valid_ptr(sp + 1))
           {
               f->eax = filesize(*(sp + 1));
           }
           break;
       case SYS_READ:
           if(is_valid_ptr(sp + 1) &&
               is_valid_ptr(sp + 2) &&
               is_valid_ptr(sp + 3))
           {
               f->eax = read(*(sp + 1),
                              (void *) *(sp + 2),
                             *(sp + 3),
                               sp);               
           }
           break;
       case SYS_WRITE :
           if(is_valid_ptr (sp + 1) &&
               is_valid_ptr (sp + 2) &&
               is_valid_ptr (sp + 3))
           {
               f->eax = write(*(sp + 1),
                               (void *) *(sp + 2),
                              *(sp + 3));
           }
           break;           
       case SYS_SEEK :
           if(is_valid_ptr (sp + 1) &&
               is_valid_ptr (sp + 2))
           {
               seek (*(sp + 1), *(sp + 2));
           }
           break;
       case SYS_TELL :
           // TODO: Do something
           break;
       case SYS_CLOSE :
           if(is_valid_ptr (sp + 1))
           {
               close(*(sp + 1));
           }
           break;
       default :
           exit(-1);
    }
}

int open (const char *file)
{
    lock_acquire(&filesys_lock);    
    struct file *f = filesys_open(file);
    lock_release(&filesys_lock);

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
   than size if some bytes could not be written. */
int write(int fd, const void *buffer, unsigned size)
{
    is_valid_fd(fd);

    /* Might cause problems if it is possible to change the meaning of
       fd 0 and fd 1. fx writing to files instead of console etc. */  

    if(!is_valid_ptr (buffer) && !is_valid_ptr (buffer + size))
    {
        exit(-1);
    }    
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
            lock_release(&filesys_lock);
            return 0;
        }

        off_t written = file_write (f, buffer, size);        
        lock_release(&filesys_lock);        
        
        return written; /* Returns number of bytes written */
    }
    
    lock_release(&filesys_lock);
    return -1;
}

/* Reads size bytes from the file open as fd into buffer.
   Returns the number of bytes actually read (0 at end of file)
   or -1 if the file could not be read (due to a condition other than end of file).
   Fd 0 reads from the keyboard using input_getc(). */
int read (int fd, void *buffer, unsigned size, void *sp) 
{
    is_valid_fd(fd);

    /* Might cause problems if it is possible to change the meaning of
       fd 0 and fd 1. fx writing to files instead of console etc. */  

    if(!is_user_vaddr(buffer))
    {
        exit(-1);
    }   
    
    struct spt_entry *page = page_lookup(buffer);
    
    /* Do we have to expand the stack? */
    if(page == NULL && sp-32 <= buffer)
    {        
        void *rd_buffer = pg_round_down(buffer);
        struct thread *cur = thread_current();
  
        int count;
        for(count = 0;
            is_user_vaddr(rd_buffer + count) &&
            page_lookup(rd_buffer + count) == NULL;
            count += PGSIZE)
        {
            
            /* Have we run out of stack space? */
            if(PHYS_BASE - buffer > MAX_STACK_SIZE)
            {
                exit(-1);
            }
            
            void *frame = vm_frame_alloc(PAL_USER | PAL_ZERO, rd_buffer);
            if(frame != NULL)
            {
                bool success = (pagedir_get_page(cur->pagedir, rd_buffer + count) == NULL &&
                                pagedir_set_page(cur->pagedir, rd_buffer + count, frame, true));
                if(success)
                {
                    insert_page(NULL,0,rd_buffer,0,0,true, SWAP);
                }else
                {
                    vm_frame_free(frame);
                }
            } 
        }
    }    
    if(fd == STDIN_FILENO) // fd = 0
    {
        uint8_t *buf = (uint8_t *) buffer;
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
        //lock_acquire(&filesys_lock);
        struct file *f = thread_current()->fdtable[fd];

        off_t read = file_read(f, buffer, size);
        
        /* Returns number of bytes read */
        //lock_release(&filesys_lock);
        return read;        
    }

    return -1;
}

void exit(int status)
{
    struct thread *cur = thread_current();
    cur->p->status = status;
    printf("%s: exit(%d)\n", cur->name, cur->p->status);

    /* Close all open files */
    int i;
    for(i = 2; i < 10; i++) // for now 0 and 1 reserved for stdin and stdout. not ideal.
    {
        close(i);
    }

    /* Release all locks */
    struct list_elem *e;
    for (e = list_begin(&cur->locks);
         e != list_end(&cur->locks);
         e = list_next(e))
    {
        struct lock *lock = list_entry(e, struct lock, lockelem);
        lock_release(lock);
    }
    
    thread_exit();
}

bool create(const char *file, unsigned initial_size)
{
    lock_acquire(&filesys_lock);
    bool result = filesys_create(file,initial_size);
    lock_release(&filesys_lock);
    return result;    
}

void close(int fd)
{
    is_valid_fd(fd);

    if(thread_current()->fdtable[fd] != NULL)
    {
        lock_acquire(&filesys_lock);        
        file_close(thread_current()->fdtable[fd]);
        lock_release(&filesys_lock);
        
        thread_current()->fdtable[fd] = NULL;
    }       
}

int filesize(int fd)
{
    is_valid_fd(fd);
    
    if(thread_current()->fdtable[fd] != NULL)
    {
        lock_acquire(&filesys_lock);
        off_t length = file_length(thread_current()->fdtable[fd]);        
        lock_release(&filesys_lock);
        
        return length;
    }

    return -1;
}

int exec(const char *cmd_line)
{
    /* The parent should wait until it knows what happened to the child,
       whether it successfully loaded its executable or failed */
    
    int pid = process_execute(pagedir_get_page (thread_current()->pagedir, cmd_line));
    struct process *p = get_child(pid);
    
    /* Wait for the child to have been properly loaded */
    sema_down(&p->load);
    
    
    /* At this point the child process has finished loading and it either failed
       or succeeded. The child could have received cpu cycles and could even have finished */
    if(p->loaded)
    {
        return pid;
    }else
    {
        return -1;
    }    
}

void seek (int fd, unsigned position)
{
    is_valid_fd(fd);
    
    if(thread_current()->fdtable[fd] != NULL)
    {
        lock_acquire(&filesys_lock);        
        file_seek(thread_current()->fdtable[fd], position);
        lock_release(&filesys_lock);
    }
}

void is_valid_fd(int fd)
{
    if(fd > MAX_OPEN_FILES) /* Max allowed files opened */
    {
        exit(-1);
    }
}

bool is_valid_ptr(const void *ptr)
{
    struct thread *t = thread_current();
    if (ptr == NULL || !is_user_vaddr (ptr) || page_lookup (ptr) == NULL)
    {
        exit (-1);
    	return false;
    }else
    {
        return true;
    }
}
