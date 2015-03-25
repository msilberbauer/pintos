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

struct list frame_table;     /* Keeps track of all the frames of physical memory used
                                by the user processes. It record the statuses of each frame
                                such as the thread it belongs to and ....
                                The frame table is necessary for physical memory allocation
                                and is used to select victim when swapping */

struct lock lock;            /* A lock for the frame_table */
struct lock eviction_lock;   /* A lock for eviction */

void frame_table_init(void)
{
    list_init (&frame_table);
    lock_init (&lock);
    lock_init (&eviction_lock);
}

/* Finds a victim page in the physical memory */
struct frame *vm_frame_pick_victim(void)
{
    /* TODO some kind of policy for eviction */
    struct list_elem *e;
    
    for (e = list_begin (&frame_table); e != list_end (&frame_table);
           e = list_next (e))
    {
        struct frame *f = list_entry (e, struct frame, elem);

        if(f != NULL)
        {
            return f;
        }        
    }

    return NULL;
}

void *vm_frame_evict(const void *new_uaddr)
{
    /* Find a victim page in the physical memory */
    struct frame *victim = vm_frame_pick_victim();
    ASSERT(victim != NULL);
   
    lock_acquire(&lock);
    bool dirty = pagedir_is_dirty(victim->thread->pagedir, victim->uaddr);
    
    struct spt_entry *spt_entry = page_lookup(victim->uaddr);
    ASSERT(spt_entry != NULL);
    if(spt_entry->type == FS)
    {
        /* If the page is FS and is dirty we write the page to the file system or swap disk */
        /* For now file system */
        if(dirty)
        {
            /* To file system */
            /*
            lock_acquire(&filesys_lock);
            file_write_at(spt_entry->file,
                          victim->uaddr,
                          spt_entry->read_bytes,
                          spt_entry->offset);
            lock_release(&filesys_lock);
            */

            /* To swap disk */
            spt_entry->type = SWAP;
            swap_write(victim->uaddr); /* Swap out the victim page to the swap disk */
        }   
    }
    if(spt_entry->type == SWAP)
    {
        /* Swap out the victim page to the swap disk */
        int bitmap_index = swap_write(victim->uaddr);
        spt_entry->bitmap_index = bitmap_index;
    }
    /* TODO If zero */
    /* TODO If memory */

    
    /* Remove references to the frame from any page table that refers to it
       NOTE: right now there is a one to one mapping */    
    pagedir_clear_page(victim->thread->pagedir, victim->uaddr);
    
    /* We update the victim frame */
    victim->thread = thread_current();
    victim->uaddr = new_uaddr;
    lock_release(&lock);
    
    return victim->kpage;
}

void *vm_frame_alloc(enum palloc_flags flags, const void *uaddr)
{
    if(!(flags & PAL_USER))
    {
        return NULL;
    }

    lock_acquire(&lock);
    void *kpage = palloc_get_page(flags);
    lock_release(&lock);
    
    /* If it is NULL. There are no free frames. we need to evict a frame. */
    if(kpage == NULL)
    {
        lock_acquire(&eviction_lock);
        kpage = vm_frame_evict(uaddr);
        lock_release(&eviction_lock);
        if(kpage == NULL)
        {
            PANIC("Could allocate frame");
        }

        return kpage;
    }else
    {    
        struct frame *f = malloc(sizeof(struct frame));

        f->thread = thread_current();
        f->kpage = kpage;
        f->uaddr = uaddr;

        lock_acquire(&lock);
        list_push_front(&frame_table, &f->elem);
        lock_release(&lock);
    
        return kpage;
    }
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


