#include "filesys/cache.h"
#include <debug.h>
#include <string.h>
#include "threads/thread.h"
#include "filesys/filesys.h"
#include "devices/timer.h"
#include "threads/synch.h"

#define CACHE_COUNT 64

static void acquire_exclusive(int i);
static void acquire_nonexclusive(int i);
static void release_exclusive(int i);
static void release_nonexclusive(int i);

struct cache_block
{
    block_sector_t sector;
    uint8_t *data;
    bool is_dirty;
    bool in_use;
    bool is_accessed;

    int readers;
    int read_waiters;
    int writers;
    int write_waiters;

    struct lock data_lock;
    struct lock rw_lock;
    struct condition read;
    struct condition write;
};

struct cache_block cache[CACHE_COUNT];
struct lock cache_lock;
static int turn = 0;
static struct list read_ahead_list;
static struct lock read_ahead_lock;
static struct condition read_ahead_list_not_empty;

struct to_read
{
    block_sector_t sector;
    struct list_elem elem;
};

void cache_init(void)
{
    lock_init(&cache_lock);
    int i;
    for(i = 0; i < CACHE_COUNT; i++)
    {
        cache[i].sector = -1;
        cache[i].data = malloc(BLOCK_SECTOR_SIZE);
        lock_init(&cache[i].data_lock);
        lock_init(&cache[i].rw_lock);
        cond_init(&cache[i].read);
        cond_init(&cache[i].write);
        cache[i].readers = 0;
        cache[i].read_waiters = 0;
        cache[i].writers = 0;
        cache[i].write_waiters = 0;
    }

    list_init(&read_ahead_list);
    lock_init(&read_ahead_lock);
    cond_init(&read_ahead_list_not_empty);
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
    int i;
    for(i = 0; i < CACHE_COUNT; i++)
    {
        acquire_nonexclusive(i);
        lock_acquire(&cache[i].data_lock);
        if(cache[i].is_dirty)
        {
            block_write(fs_device, cache[i].sector, cache[i].data);
            cache[i].is_dirty = false;
        }
        lock_release(&cache[i].data_lock);
        release_nonexclusive(i);
    }
}

int cache_find_block(block_sector_t sector)
{
    int i;
    for(i = 0; i < CACHE_COUNT; i++) /* If it as already in cache */
    {
        lock_acquire(&cache[i].rw_lock);
        if(cache[i].sector == sector)
        {
            lock_release(&cache[i].rw_lock);
            return i;
        }
        lock_release(&cache[i].rw_lock);
    }

    for(i = 0; i < CACHE_COUNT; i++) /* If there is a free block */
    {
        lock_acquire(&cache[i].rw_lock);
        if(!cache[i].in_use)
        {
            lock_release(&cache[i].rw_lock);
            return i;
        }
        lock_release(&cache[i].rw_lock);
        
    }

    return cache_evict(); /* We have to evict */
}

int cache_evict (void)
{
    while(true)
    {
        turn++;
        if(turn >= CACHE_COUNT)
        {
            turn = 0;
        }
        
        lock_acquire(&cache[turn].rw_lock);
        if(cache[turn].readers ||
           cache[turn].writers ||
           cache[turn].read_waiters ||
           cache[turn].write_waiters)
        {
            /* We do not evict */
            lock_release(&cache[turn].rw_lock);
            continue;
        }

        if(cache[turn].is_accessed)
        {
            cache[turn].is_accessed = false;
        }else
        {
            break;
        }
        
        lock_release(&cache[turn].rw_lock);
    }
 
    lock_release(&cache[turn].rw_lock);
    lock_acquire(&cache[turn].data_lock);
    if(cache[turn].is_dirty)
    {
        block_write(fs_device, cache[turn].sector, cache[turn].data);
    }
    
    cache[turn].in_use = false;
    cache[turn].is_dirty = false;
    lock_release(&cache[turn].data_lock);

    return turn;
}

static void acquire_nonexclusive(int i)
{
    lock_acquire(&cache[i].rw_lock);
    if(cache[i].writers || cache[i].write_waiters)
    {
        ++cache[i].read_waiters;
        do
        {
            cond_wait(&cache[i].read, &cache[i].rw_lock);
        }while(cache[i].writers || cache[i].write_waiters);
        
        --cache[i].read_waiters;
    }

    ++cache[i].readers;

    lock_release(&cache[i].rw_lock);
}

static void release_nonexclusive(int i)
{
    lock_acquire(&cache[i].rw_lock);
    --cache[i].readers;
    if(cache[i].write_waiters)
    {
        cond_signal(&cache[i].write, &cache[i].rw_lock);
    }
    
    lock_release(&cache[i].rw_lock);
}

void cache_read(block_sector_t sector, uint8_t *buffer)
{
    cache_read_partial(sector, buffer, 0, BLOCK_SECTOR_SIZE);
}

void cache_read_partial(block_sector_t sector, uint8_t *buffer, int offset, int chunk_size)
{
    lock_acquire(&cache_lock);
    int i = cache_find_block(sector);
    lock_release(&cache_lock);
    
    acquire_nonexclusive(i);
    
    lock_acquire(&cache[i].data_lock);
    if(!cache[i].in_use)
    {
        cache[i].sector = sector;
        cache[i].is_dirty = false;
        cache[i].in_use = true;
        block_read(fs_device, sector, cache[i].data);
    }

    cache[i].is_accessed = true;
    memcpy(buffer, cache[i].data + offset, chunk_size);
    lock_release(&cache[i].data_lock);

    release_nonexclusive(i);
}

static void acquire_exclusive(int i)
{
    lock_acquire(&cache[i].rw_lock);
    if(cache[i].readers || cache[i].writers)
    {
        ++cache[i].write_waiters;
        do
        {
            cond_wait(&cache[i].write, &cache[i].rw_lock);
        }while(cache[i].readers || cache[i].writers);
        
        --cache[i].write_waiters;
    }
    
    cache[i].writers = 1;
    lock_release(&cache[i].rw_lock);    
}

static void release_exclusive(int i)
{
    lock_acquire(&cache[i].rw_lock);
    cache[i].writers = 0;
    if(cache[i].write_waiters)
    {
        cond_signal(&cache[i].write, &cache[i].rw_lock);
    }
    else
    {
        cond_broadcast(&cache[i].read, &cache[i].rw_lock);
    }
    lock_release(&cache[i].rw_lock);
}

void cache_write(block_sector_t sector, uint8_t *buffer)
{
    cache_write_partial(sector, buffer, 0, BLOCK_SECTOR_SIZE);
}

void cache_write_partial(block_sector_t sector, uint8_t *buffer, int offset, int chunk_size)
{
    lock_acquire(&cache_lock);
    int i = cache_find_block(sector);
    lock_release(&cache_lock);

    acquire_exclusive(i);
    
    lock_acquire(&cache[i].data_lock);
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

    lock_release(&cache[i].data_lock);

    release_exclusive(i);
}

/* Flush daemon */
void flush_daemon(void *aux UNUSED)
{
    while(true)
    {
        timer_msleep(30 * 1000);
        cache_flush();
    }
}

/* Readahead daemon */
void read_ahead_daemon(void *aux UNUSED)
{
    while(true)
    {
        lock_acquire(&read_ahead_lock);
        while(list_empty(&read_ahead_list))
        {
            cond_wait(&read_ahead_list_not_empty, &read_ahead_lock);
        }
        struct to_read *tr = list_entry(list_pop_front (&read_ahead_list),
                                         struct to_read, elem);
        lock_release(&read_ahead_lock);

        lock_acquire(&cache_lock);
        int i = cache_find_block(tr->sector);
        lock_release(&cache_lock); 

        acquire_nonexclusive(i);
        
        lock_acquire(&cache[i].data_lock);
        if(!cache[i].in_use)
        {
            cache[i].sector = tr->sector;
            cache[i].is_dirty = false;
            cache[i].in_use = true;
            block_read(fs_device, tr->sector, cache[i].data);
        }

        cache[i].is_accessed = true;
        lock_release(&cache[i].data_lock);

        release_nonexclusive(i);
    }
}

void read_ahead_request(block_sector_t sector)
{
    struct to_read *tr = (struct to_read *) malloc(sizeof(struct to_read));
    ASSERT(tr != NULL);

    tr->sector = sector;
    
    lock_acquire(&read_ahead_lock);
    list_push_back(&read_ahead_list, &tr->elem);
    cond_signal(&read_ahead_list_not_empty, &read_ahead_lock);
    lock_release(&read_ahead_lock);    
}

