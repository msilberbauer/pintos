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
#include "threads/interrupt.h"

struct list frame_table;      /* Keeps track of all the frames of physical memory used
                                 by the user processes. It record the statuses of each frame
                                 such as the thread it belongs to and ....
                                 The frame table is necessary for physical memory allocation
                                 and is used to select victim when swapping */

struct lock frame_table_lock; /* A lock for the frame_table */

void frame_table_init(void)
{
    list_init(&frame_table);
    lock_init(&frame_table_lock);
}

/* Second chance page replacement algorithm
   Finds a victim page in the physical memory */
struct frame_entry *frame_pick_victim(void)
{
    struct frame_entry *victim;
    struct list_elem *e;
    while(true)
    {
        /* Best candidates */
        for (e = list_begin (&frame_table); e != list_end (&frame_table);
             e = list_next (e))
        {
            victim = list_entry (e, struct frame_entry, elem);
            if(!victim->spte->pinned)
            {
                if (!pagedir_is_dirty (victim->thread->pagedir, victim->spte->uaddr) &&
                    !pagedir_is_accessed (victim->thread->pagedir, victim->spte->uaddr))
                {         
                    return victim;
                }
            }            
        }

        /* Worse candidates. */
        for (e = list_begin (&frame_table); e != list_end (&frame_table);
             e = list_next (e))
        {
            victim = list_entry (e, struct frame_entry, elem);
            if(!victim->spte->pinned)
            {
                if (pagedir_is_dirty (victim->thread->pagedir, victim->spte->uaddr) &&
                    !pagedir_is_accessed (victim->thread->pagedir, victim->spte->uaddr))
                {
                    return victim;
                }
            }            

            /* Set candidate to not accessed in order to evict it later possibly */
            pagedir_set_accessed (victim->thread->pagedir, victim->spte->uaddr, false);
        }
    }

    return NULL;
}

void *frame_evict(enum palloc_flags flags)
{
    struct frame_entry *victim = frame_pick_victim();   
    
    bool dirty = pagedir_is_dirty(victim->thread->pagedir, victim->spte->uaddr);     
    if(victim->spte->type == FS)
    {
        /* If the page is FS and is dirty we write the page swap disk */
        if(dirty)
        {     
            victim->spte->type = SWAP;
            int bitmap_index = swap_write(victim->kpage);
            victim->spte->bitmap_index = bitmap_index;            
        }   
    }
    else if(victim->spte->type == SWAP)
    {
        /* Swap out the victim page to the swap disk */
        int bitmap_index = swap_write(victim->kpage);
        victim->spte->bitmap_index = bitmap_index;
        
    }else if(victim->spte->type == MMAP)
    {
        if(dirty)
        {
            /* To file system */            
            file_write_at(victim->spte->file,
                          victim->spte->uaddr,
                          victim->spte->read_bytes,
                          victim->spte->offset);
        }        
    }

    victim->spte->loaded = false;
    list_remove(&victim->elem);
    pagedir_clear_page(victim->thread->pagedir, victim->spte->uaddr);
    palloc_free_page(victim->kpage);
    free(victim);
    return palloc_get_page(flags);
}

void frame_free(void *frame)
{
    struct list_elem *e = list_begin(&frame_table);
    struct list_elem *next;
    lock_acquire(&frame_table_lock);
    while(e != list_end(&frame_table))
    {
        next = list_next(e);
        struct frame_entry *fte = list_entry(e, struct frame_entry, elem);
        if (fte->kpage == frame)
	{
            list_remove(e);
            free(fte);
            palloc_free_page(frame);
	}

        e = next;
    }
    lock_release(&frame_table_lock);
}

void *frame_alloc(enum palloc_flags flags, struct spt_entry *spte)
{
    if(!(flags & PAL_USER))
    {
        return NULL;
    }
    
    void *kpage = palloc_get_page(flags);
    /* If it is NULL. There are no free frames. we need to evict a frame. */
    if(kpage == NULL)
    {
        lock_acquire(&frame_table_lock);
        kpage = frame_evict(flags);
        while(kpage == NULL)
        {
            kpage = frame_evict(flags);
        }
        lock_release(&frame_table_lock);
     
    }
    
    struct frame_entry *fte = malloc(sizeof(struct frame_entry));
    fte->kpage = kpage;
    fte->spte = spte;
    fte->thread = thread_current();

    lock_acquire(&frame_table_lock);
    list_push_back(&frame_table, &fte->elem);
    lock_release(&frame_table_lock);

    return kpage;
}
