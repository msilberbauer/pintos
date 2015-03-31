#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "filesys/file.h"
#include "lib/kernel/hash.h"
#include <hash.h>
#include "threads/thread.h"
#include "userprog/exception.h"

/* Where the data is stored */
enum spt_type
{
    SWAP,
    FS,
    MMAP
};

struct spt_entry
{
    uint8_t *uaddr;          /* Page address */
    bool writable;           /* Whether the pages initialized should be writable */
    bool loaded;             /* Whether this page has finished loading or not */
    bool pinned;             /* Whether this page is locked or not */

    /* Used if type is FS */
    struct file *file;       /* The file to be read from */
    size_t read_bytes;       /* The number of bytes read from executable file */
    size_t zero_bytes;       /* The number of bytes to initialize to zero */
    off_t offset;            /* Combined with page number obtain the physical
                                address */
    
    /* Used if type is SWAP */
    size_t bitmap_index;     /* The bitmap index of this page that has been swapped out */
    
    struct hash_elem elem;   /* Used to insert in hash table */
    enum spt_type type;      /* The type, where the data is stored */
};

bool insert_file_spte(struct file *file, off_t offset, uint8_t *uaddr,
		      size_t read_bytes, size_t zero_bytes, bool writable);
bool insert_mmap_spte(struct file *file, off_t offset, uint8_t *uaddr,
                      size_t read_bytes, size_t zero_bytes);


struct spt_entry *spte_lookup(void *uaddr);
bool grow_stack(void *uaddr);

bool load_page(struct spt_entry *spte);
bool load_swap(struct spt_entry *spte);
bool load_file(struct spt_entry *spte);

void init_spt(struct hash *spt);
void destroy_spt(struct hash *spt);
void remove_spte(struct hash *spt, struct spt_entry *spte);
#endif
