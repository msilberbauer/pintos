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
int open(const char *file);
int write(int fd, const void *buffer, unsigned size);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size, void * sp);
void exit(int status);
bool create(const char *file, unsigned initial_size);
void close(int fd);
void is_valid_fd(int fd);
int exec(const char *cmd_line);
void seek(int fd, unsigned position);
bool remove(const char *file);
unsigned tell(int fd);
bool is_valid_ptr(const void *ptr);
void is_valid_buffer(void *buffer, unsigned size, bool to_write);
void is_valid_string(const void *string);

void syscall_init(void)
{
    lock_init(&filesys_lock);
    intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

uint32_t *sp;

/* The system call handler. System calls that return a value can do so
   by modifying the "eax" member of struct intr_frame. */
static void syscall_handler(struct intr_frame *f UNUSED)
{  
    sp = f->esp;
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
               is_valid_string((char *) *(sp + 1));
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
               is_valid_string((char *) *(sp + 1));
               f->eax = create((char *) *(sp + 1), *(sp + 2));
           }
           break;
       case SYS_REMOVE :
           if(is_valid_ptr (sp + 1))
           {
               is_valid_ptr((char *) *(sp + 1));
               is_valid_string((char *) *(sp + 1));
               f->eax = remove((char *) *(sp + 1));               
           }
           break;           
       case SYS_OPEN :
           if(is_valid_ptr(sp + 1))
           {
               is_valid_ptr((const char *) *(sp + 1));
               is_valid_string((const char *) *(sp + 1));
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
               seek(*(sp + 1), *(sp + 2));
           }
           break;
       case SYS_TELL :
           if(is_valid_ptr (sp + 1))
           {
               tell(*(sp + 1));
           }           
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

    if(f != NULL)
    {
        int i;
        for(i = 2; i < 10; i++) // for now 0 and 1 reserved for stdin and stdout. not ideal.
        {
            if(thread_current()->fdtable[i] == NULL)
            {
                thread_current()->fdtable[i] = f;
                lock_release(&filesys_lock);
                return i;
            }
        }
    }    
    lock_release(&filesys_lock);
    return -1;
}

/* Writes size bytes from buffer to the open file fd.
   Returns the number of bytes actually written, which may be less
   than size if some bytes could not be written. */
int write(int fd, const void *buffer, unsigned size)
{
    is_valid_fd(fd);
    is_valid_buffer(buffer, size, false);
    
    /* Might cause problems if it is possible to change the meaning of
       fd 0 and fd 1. fx writing to files instead of console etc. */  
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

        off_t written = file_write(f, buffer, size);        
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
    is_valid_buffer(buffer, size, true);
    
    /* Might cause problems if it is possible to change the meaning of
       fd 0 and fd 1. fx writing to files instead of console etc. */      
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
        if(f == NULL)
        {
            return -1;
            //lock_release(&filesys_lock);
        }
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
    struct list_elem *next;
    struct list_elem *e = list_begin(&cur->locks);
    while(e != list_end (&cur->locks))
    {
        next = list_next(e);
        struct lock *l = list_entry (e, struct lock, lockelem);
        lock_release(l);
        list_remove(&l->lockelem);
        e = next;
    }

    /* Remove all child its child processes */
    e = list_begin(&cur->children);
    while(e != list_end(&cur->children))
    {
        next = list_next(e);
        struct process *cp = list_entry(e, struct process, elem);
        list_remove(&cp->elem);
        free(cp);
        e = next;
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
    ASSERT(p != NULL);
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

void seek(int fd, unsigned position)
{
    is_valid_fd(fd);
    
    if(thread_current()->fdtable[fd] != NULL)
    {
        lock_acquire(&filesys_lock);        
        file_seek(thread_current()->fdtable[fd], position);
        lock_release(&filesys_lock);
    }
}

bool remove(const char *file)
{
    lock_acquire(&filesys_lock);
    bool result = filesys_remove(file);
    lock_release(&filesys_lock);
    return result;
}

unsigned tell(int fd)
{
    lock_acquire(&filesys_lock);
    is_valid_fd(fd);
    if(thread_current()->fdtable[fd] != NULL)
    {               
        unsigned offset = file_tell(thread_current()->fdtable[fd]);
        lock_release(&filesys_lock);
        return offset;
    }

    lock_release(&filesys_lock);
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
    if(!is_user_vaddr(ptr) || ptr < 0x08048000)
    {
        exit(-1);
    	return false;
    }
    bool success = false;
    struct spt_entry *spte = spte_lookup(ptr);
    if(spte)
    {
        load_page(spte);
        success = spte->loaded;
    }
    else if(ptr >= sp - 32)
    {
        success = grow_stack((void *) ptr);
    }
    if(!success)
    {        
        exit(-1);
    }
    
    return true;
}

void is_valid_buffer(void *buffer, unsigned size, bool to_write)
{    
    char *buf = (char *) buffer;
    unsigned i;
    for(i = 0; i < size; i++)
    {
        if(is_valid_ptr((const void *) buf))
        {
            struct spt_entry *spte = spte_lookup((const void *) buf);
            if(spte && to_write)
            {
                if(!spte->writable)
                {
                    exit(-1);
                }
            }            
        }

        buf++;
    }
}

void is_valid_string(const void *string)
{
    is_valid_ptr(string);
    while(*(char *) string != 0)
    {
        string = (char *) string + 1;
        is_valid_ptr(string);
    }
}
