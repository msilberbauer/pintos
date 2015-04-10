#include "filesys/cache.h"
#include <debug.h>
#include <string.h>
#include "threads/thread.h"
#include "filesys/filesys.h"
#include "devices/timer.h"
#include "threads/synch.h"

#define CACHE_COUNT 64

struct cache_block
{
    block_sector_t sector;
    uint8_t *data;
    bool is_dirty;
    bool in_use;
    bool is_accessed;
};

struct cache_block cache[CACHE_COUNT];
struct lock cache_lock;
static int turn = 0;

void cache_init(void)
{
    lock_init(&cache_lock);
    int i;
    for(i = 0; i < CACHE_COUNT; i++)
    {
        cache[i].data = malloc(BLOCK_SECTOR_SIZE);
    }
}

void cache_done(void)
{
    cache_flush();
    int i;
    for(i = 0; i < CACHE_COUNT; i++)
    {
        free(cache[i].data);
    }
}

void cache_flush(void)
{
    lock_acquire(&cache_lock);
    int i;
    for(i = 0; i < CACHE_COUNT; i++)
    {
        if(cache[i].is_dirty)
        {
            block_write(fs_device, cache[i].sector, cache[i].data);
            cache[i].is_dirty = false;
        }
    }
    lock_release(&cache_lock);
}

int cache_find_block(block_sector_t sector)
{
    int i;
    for(i = 0; i < CACHE_COUNT; i++) /* If it as already in cache */
    {
        if(cache[i].sector == sector)
        {
            return i;
        }
    }

    for(i = 0; i < CACHE_COUNT; i++) /* If there is a free block */
    {
        if(!cache[i].in_use)
        {
            return i;
        }
    }

    return cache_evict(); /* We have to evict */
}

int cache_evict (void)
{
    while(true)
    {
        if(cache[turn].is_accessed)
        {
            cache[turn].is_accessed = false;
        }else
        {
            break;
        }

        turn ++;
        if(turn >= CACHE_COUNT)
        {
            turn = 0;
        }
    }
    
    if(cache[turn].is_dirty)
    {
        block_write(fs_device, cache[turn].sector, cache[turn].data);
    }
    
    cache[turn].in_use = false;
    cache[turn].is_dirty = false;
    
    return turn;
}

void cache_read(block_sector_t sector, uint8_t *buffer)
{
    cache_read_partial(sector, buffer, 0, BLOCK_SECTOR_SIZE);
}

void cache_read_partial(block_sector_t sector, uint8_t *buffer, int offset, int chunk_size)
{
    lock_acquire(&cache_lock);
    int i = cache_find_block(sector);

    if(!cache[i].in_use)
    {
        cache[i].sector = sector;
        cache[i].is_dirty = false;
        cache[i].in_use = true;
        block_read(fs_device, sector, cache[i].data);
    }

    cache[i].is_accessed = true;
    memcpy(buffer, cache[i].data + offset, chunk_size);
    lock_release(&cache_lock);
}

void cache_write(block_sector_t sector, uint8_t *buffer)
{
    cache_write_partial(sector, buffer, 0, BLOCK_SECTOR_SIZE);
}

void cache_write_partial(block_sector_t sector, uint8_t *buffer, int offset, int chunk_size)
{
    lock_acquire(&cache_lock);
    int i = cache_find_block(sector); 
 
    if(!cache[i].in_use)
    {
        cache[i].sector = sector;
        cache[i].in_use = true;
    }

    cache[i].is_dirty = true;
    cache[i].is_accessed = true;
    if(buffer)
    {
        memcpy(cache[i].data + offset, buffer, chunk_size);
    }
    else
    {
        memset(cache[i].data + offset, 0, chunk_size);
    }
    lock_release(&cache_lock);
}

