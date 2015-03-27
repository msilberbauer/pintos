#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/palloc.h"
#include "lib/kernel/list.h"
#include "userprog/pagedir.h"
#include "vm/page.h"
#include "threads/vaddr.h"

bool insert_page(struct file *file, off_t offset, uint8_t *upage,
                 size_t read_bytes, size_t zero_bytes, bool writable, enum spt_type type)
{
    struct thread *t = thread_current();
    
    struct spt_entry *e = malloc(sizeof(struct spt_entry));
    e->file = file;
    e->offset = offset;
    e->upage = upage;
    e->read_bytes = read_bytes;
    e->zero_bytes = zero_bytes;
    e->writable = writable;
    e->type = type;
    e->loaded = true;
    
    return hash_insert(&t->spt, &e->elem) == NULL;
}

/* Returns the page containing the given virtual address,
   or a null pointer if no such page exists. From Pintos manual */
struct spt_entry *page_lookup(const void *address)
{
    struct thread *t = thread_current();
    
    struct spt_entry p;
    struct hash_elem *e;
  
    p.upage = pg_round_down(address);
    e = hash_find (&t->spt, &p.elem);
    return e != NULL ? hash_entry (e, struct spt_entry, elem) : NULL;
}

/* Returns a hash value for page p. From Pintos manual */
unsigned spt_entry_hash (const struct hash_elem *p_, void *aux UNUSED)
{
    const struct spt_entry *p = hash_entry (p_, struct spt_entry, elem);
    return hash_bytes (&p->upage, sizeof p->upage);
}

/* Returns true if page a precedes page b. From Pintos manual */
bool spt_entry_less (const struct hash_elem *a_, const struct hash_elem *b_,
           void *aux UNUSED)
{
    const struct spt_entry *a = hash_entry (a_, struct spt_entry, elem);
    const struct spt_entry *b = hash_entry (b_, struct spt_entry, elem);

    return a->upage < b->upage;
}

void free_spt_entry(struct hash_elem *elem, void *aux UNUSED)
{
    struct spt_entry *page = hash_entry(elem, struct spt_entry, elem);
    free(page);
}

void free_spt(struct hash *spt)
{
    hash_destroy(spt, free_spt_entry);
}
