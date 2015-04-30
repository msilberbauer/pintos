#include <debug.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/palloc.h"
#include "lib/kernel/list.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "userprog/process.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"
#include "threads/vaddr.h"

bool insert_file_spte(struct file *file, off_t offset, uint8_t *uaddr,
		      size_t read_bytes, size_t zero_bytes, bool writable)
{
    struct spt_entry *spte = malloc(sizeof(struct spt_entry));
    if(spte == NULL)
    {
        return false;
    }
    spte->file = file;
    spte->offset = offset;
    spte->uaddr = uaddr;
    spte->read_bytes = read_bytes;
    spte->zero_bytes = zero_bytes;
    spte->writable = writable;
    spte->loaded = false;
    spte->type = FS;
    spte->pinned = false;

    return hash_insert(&thread_current()->spt, &spte->elem) == NULL;
}

bool insert_mmap_spte(struct file *file, off_t offset, uint8_t *uaddr,
                      size_t read_bytes, size_t zero_bytes)
{
    struct spt_entry *spte = malloc(sizeof(struct spt_entry));
    if(spte == NULL)
    {
        return false;
    }
    spte->file = file;
    spte->offset = offset;
    spte->uaddr = uaddr;
    spte->read_bytes = read_bytes;
    spte->zero_bytes = zero_bytes;
    spte->writable = true;
    spte->loaded = false;
    spte->type = MMAP;
    spte->pinned = false;

    struct mmap_file *mmap = malloc(sizeof(struct mmap_file));
    if(mmap == NULL)
    {
        free(spte);
        return false;
    }
    
    mmap->spte = spte;
    mmap->mmid = thread_current()->cur_mmapid;
    list_push_back(&thread_current()->mmaps, &mmap->elem);
    
    return hash_insert(&thread_current()->spt, &spte->elem) == NULL;
}

struct spt_entry *spte_lookup(void *uaddr)
{    
    struct spt_entry p;
    struct hash_elem *e;
    p.uaddr = pg_round_down(uaddr);
    e = hash_find(&thread_current()->spt, &p.elem);
    return e != NULL ? hash_entry (e, struct spt_entry, elem) : NULL;
}

static unsigned spte_hash_func(const struct hash_elem *e, void *aux UNUSED)
{
    struct spt_entry *spte = hash_entry(e, struct spt_entry,
                                             elem);
    return hash_bytes (&spte->uaddr, sizeof spte->uaddr);
}

static bool spte_less_func(const struct hash_elem *a_,
			   const struct hash_elem *b_,
			   void *aux UNUSED)
{
    const struct spt_entry *a = hash_entry (a_, struct spt_entry, elem);
    const struct spt_entry *b = hash_entry (b_, struct spt_entry, elem);
    return a->uaddr < b->uaddr;
}

bool grow_stack(void *uaddr)
{
    /* is the stack full? */
    if((size_t) (PHYS_BASE - pg_round_down(uaddr)) > MAX_STACK_SIZE)
    {
        return false;
    }

    struct spt_entry *spte = malloc(sizeof(struct spt_entry));
    if(spte == NULL)
        return false;
    
    spte->uaddr = pg_round_down(uaddr);
    spte->loaded = true;
    spte->writable = true;
    spte->type = SWAP;
    spte->pinned = true;

    uint8_t *frame = frame_alloc(PAL_USER, spte);
    if(frame == NULL)
    {
        free(spte);
        return false;
    }

    if(!install_page(spte->uaddr, frame, spte->writable))
    {
        free(spte);
        frame_free(frame);
        return false;
    }

    return hash_insert(&thread_current()->spt, &spte->elem) == NULL;
}

static void free_spte(struct hash_elem *e, void *aux UNUSED)
{
    struct spt_entry *spte = hash_entry(e, struct spt_entry, elem);
    if(spte->loaded)
    {
        frame_free(pagedir_get_page(thread_current()->pagedir, spte->uaddr));
        pagedir_clear_page(thread_current()->pagedir, spte->uaddr);
    }
    free(spte);
}

void init_spt(struct hash *spt)
{
    hash_init(spt, spte_hash_func, spte_less_func, NULL);
}

void destroy_spt(struct hash *spt)
{
    hash_destroy(spt, free_spte);
}

void remove_spte(struct hash *spt, struct spt_entry *spte)
{
    hash_delete(spt, &spte->elem);
}

bool load_page(struct spt_entry *spte)
{    
    if(spte->loaded)
        return false;
    
    spte->pinned = true;
    bool success = false;
    switch(spte->type)
    {
        case FS:
            success = load_file(spte);
            break;
        case SWAP:
            success = load_swap(spte);
            break;
        case MMAP:
            success = load_file(spte);
            break;
    }
    spte->pinned = false;
    return success;
}

bool load_swap(struct spt_entry *spte)
{
    uint8_t *frame = frame_alloc(PAL_USER, spte);
    if (!frame)
    {
        return false;
    }
    if (!install_page(spte->uaddr, frame, spte->writable))
    {
        frame_free(frame);
        return false;
    }
    swap_read(spte->bitmap_index, spte->uaddr);
    spte->loaded = true;
    return true;
}

bool load_file(struct spt_entry *spte)
{    
    uint8_t *frame = frame_alloc(PAL_USER, spte);
    if(!frame)
    {
        return false;
    }

    if(spte->read_bytes != file_read_at(spte->file, frame,
                                        spte->read_bytes,
                                        spte->offset))
    {
        frame_free(frame);
        return false;
    }

    memset(frame + spte->read_bytes, 0, spte->zero_bytes); 

    if(!install_page(spte->uaddr, frame, spte->writable))
    {
        frame_free(frame);
        return false;
    }

    spte->loaded = true;  
    return true;
}
