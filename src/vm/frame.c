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

/* Finds a victim page in the physical memory */
struct frame *vm_frame_pick_victim(void)
{
    /* TODO some kind of policy for eviction */

    struct list_elem *e;

    lock_acquire(&lock);
    for (e = list_begin (&frame_table); e != list_end (&frame_table);
           e = list_next (e))
    {
        struct frame *f = list_entry (e, struct frame, elem);

        if(f != NULL)
        {
            lock_release(&lock);
            return f;
        }        
    }

    lock_release(&lock);
    return NULL;
}

void *vm_frame_evict(const void *uaddr)
{
    /* Find a victim page in the physical memory */
    lock_acquire(&lock);
    struct frame *victim = vm_frame_pick_victim();
    lock_acquire(&lock);
      

    /* Remove references to the frame from any page table that refers to it
       NOTE: right now there is a one to one mapping */
    pagedir_clear_page(victim->thread->pagedir, victim->uaddr);

    /* If the frame is modified, write the page to the file system or to the swap disk */
    bool dirty = pagedir_is_dirty(victim->thread->pagedir, victim->uaddr);

    struct spt_entry *spt_entry = page_lookup(uaddr);
    if(spt_entry->type == FS)
    {
        if(dirty)
        {
            spt_entry->type = SWAP; /* The supplemental page entry should indicate the
       victim page has been swapped out */
            swap_write(victim->uaddr); /* Swap out the victim page to the swap disk */
        }   
    }
    if(spt_entry->type == SWAP)
    {
        swap_write(victim->uaddr); /* Swap out the victim page to the swap disk */
    }

    /* We update the victim frame */
    victim->thread = thread_current();
    victim->uaddr = uaddr;

    return victim->kpage;

    
}

void *vm_frame_alloc(enum palloc_flags flags, const void *uaddr)
{
    void *kpage = palloc_get_page(flags);

    /* If it is NULL. There are no free frames. we need to evict a frame. */
    if(kpage == NULL)
    {
        kpage = vm_frame_evict(uaddr);
        if(kpage == NULL)
        {
            PANIC("Could allocate frame");
        }        
    }

    if(!(flags & PAL_USER))
    {
        return NULL;
    }
    
    struct frame *f = malloc(sizeof(struct frame));

    f->thread = thread_current();
    f->kpage = kpage;
    f->uaddr = uaddr;

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


