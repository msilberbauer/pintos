#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "filesys/file.h"
#include "lib/kernel/hash.h"
#include <hash.h>
#include "threads/thread.h"

/* Where the data is stored */
enum spt_type
{
    SWAP,
    FS,
    ZERO
};

struct spt_entry
{ 
    uint8_t *upage;          /* Page address */
    bool writable;           /* Whether the pages initialized should be writable */

    /* Used if type is FS */
    struct file *file;       /* The file to be read from */
    size_t read_bytes;       /* The number of bytes read from executable file */
    size_t zero_bytes;       /* The number of bytes to initialize to zero */
    off_t offset;            /* Combined with page number obtain the physical
                                address */
    
    // Used if type is SWAP */
    int swap_page;           /* The victim page that has been swapped out */
    
    struct hash_elem elem;   /* Used to insert in hash table */
    enum spt_type type;      /* The type, where the data is stored */
};

unsigned spt_entry_hash (const struct hash_elem *, void *);
bool spt_entry_less(const struct hash_elem *, const struct hash_elem *, void *aux);
bool insert_page(struct file *file, off_t offset, uint8_t *upage,
                 uint32_t read_bytes, uint32_t zero_bytes, bool writable, enum spt_type type);
struct spt_entry *page_lookup (const void *address);

#endif
