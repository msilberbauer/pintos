#include "threads/synch.h"
#include "filesys/file.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"

struct list frame_table;     /* Keeps track of all the frames of physical memory used
                                by the user processes. It record the statuses of each frame
                                such as the thread it belongs to and ....
                                The frame table is necessary for physical memory allocation
                                and is used to select victim when swapping */

struct lock lock;            /* A lock for the frame_table */

void frame_table_init(void)
{
    list_init (&frame_table);
    lock_init (&lock);
}


void *vm_frame_alloc(enum palloc_flags flags)
{
    void *kpage = palloc_get_page(flags);
    
    if(kpage == NULL)
    {
        return NULL;
    }

    if(!(flags & PAL_USER))
    {
        return NULL;
    }
    
    struct frame *f = malloc(sizeof(struct frame));

    f->thread = thread_current();
    f->kpage = kpage;

    lock_acquire(&lock);
    list_push_back (&frame_table, &f->elem);
    lock_release(&lock);
    
    return kpage;
}


void *vm_frame_free(void *frame)
{
    struct list_elem *e;

    lock_acquire(&lock);
    for (e = list_begin (&frame_table); e != list_end (&frame_table);
           e = list_next (e))
    {
        struct frame *f = list_entry (e, struct frame, elem);

        if(f->kpage == frame)
        {
            list_remove(e);
            free(f);
            palloc_free_page(frame);
            break;
        }        
    }

    lock_release(&lock);
}


