#include "vm/swap.h"

struct block *swap_block;
static struct bitmap *bitmap; /* When bit is true it's free,
                                 when bit is false it's not free */
struct lock swap_lock;

size_t sectors_per_page;

void swap_init(void)
{
    swap_block = block_get_role(BLOCK_SWAP);
    ASSERT(swap_block != NULL);

    /* BLOCK_SECTOR_SIZE is size of a block device sector in bytes (512)
       PGSIZE is bytes in a page */
    sectors_per_page = PGSIZE / BLOCK_SECTOR_SIZE;
    
    /* block_size(swap_block) is the number of sectors in swap_block */ 

    bitmap = bitmap_create(block_size(swap_block) / sectors_per_page);
    ASSERT(bitmap != NULL);
    bitmap_set_all(bitmap, true);
    lock_init(&swap_lock);
}

void swap_read(size_t bitmap_index, void *upage)
{
    lock_acquire(&swap_lock);
    ASSERT(bitmap_test(bitmap, bitmap_index) != true); /* Should not be free */

    /* Example: if sectors_per_page is 2.
       Then when reading from bitmap_index 0 it should read blocksectors 0 and 1
       Reading from bitmap_index 1 then it should read blocksectors 2 and 3...
       and so on */
    size_t i;
    for(i = 0; i < sectors_per_page; i++)
    {
        block_read(swap_block,
                   bitmap_index * sectors_per_page + i,
                   upage + i * BLOCK_SECTOR_SIZE);
    }

    /* The swap slot is now considered free as we have read it into memory */
    bitmap_set(bitmap, bitmap_index, true);
    lock_release(&swap_lock);
}

size_t swap_write(void *upage)
{
    lock_acquire(&swap_lock);
    /* Search for one free (true) bit from the beginning of the bitmap */
    size_t bitmap_index = bitmap_scan(bitmap, 0, 1, true);
    ASSERT(bitmap_index != BITMAP_ERROR);
    size_t i;
    for(i = 0; i < sectors_per_page; i++)
    {
        block_write(swap_block,
                    bitmap_index * sectors_per_page + i,
                    upage + i * BLOCK_SECTOR_SIZE);
    }

    /* The swap slot is now considered occupied as we have swapped it out
       of memory into the swap slot */
    bitmap_set(bitmap, bitmap_index, false);
    lock_release(&swap_lock);
    
    return bitmap_index;
}

void swap_remove(int bitmap_index)
{
    /* Note: consider nulling sector data */
    lock_acquire(&swap_lock);
    bitmap_set(bitmap, bitmap_index, true);
    lock_release(&swap_lock);
}
