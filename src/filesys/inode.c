#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/cache.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define DIRECT_COUNT 122

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
{
//    block_sector_t start;               /* First data sector. */
    off_t length;                       /* File size in bytes. */
    block_sector_t sectors[DIRECT_COUNT];
    block_sector_t indirect;
    block_sector_t double_indirect;
    int allocated_count;              /* Number of allocated sectors */
    unsigned magic;                   /* Magic number. */

    uint32_t unused[1];               /* Not used. */
};

/* indirect and double indirect */
struct inode_disk_list
{
    block_sector_t sectors[128];
};


/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t bytes_to_sectors (off_t size)
{
    return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode
{
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
};


bool grow(struct inode_disk *disk_inode, off_t length)
{
    int i;
    static char zeros[BLOCK_SECTOR_SIZE];
    
    size_t target_sectors = bytes_to_sectors(length);
    size_t cur_sectors = bytes_to_sectors(disk_inode->length);

    /* Direct sectors */
    for(i = cur_sectors ; cur_sectors < DIRECT_COUNT && cur_sectors < target_sectors; i++)
    {
        if(free_map_allocate(1, &inode_disk->sectors[i])
        {
            cache_write(inode_disk->sectors[i], zeros);
            cur_sectors++;
        }else
        {
            // TODO shrink maybe?
            return false;
        }        
    }

        
    /* Indirect sectors */
    if(cur_sectors < DIRECT_COUNT + 128)
    {
        struct inode_disk_list *indirect = calloc(1, sizeof(struct inode_disk_list));
        if(indirect == NULL)
        {
            // TODO shrink maybe?
            return false;
        }        
        if(disk_inode->indirect == 0)
        {             
            if(!free_map_allocate(1, &disk_inode->indirect))
            {
                // TODO shrink maybe?
                return false;
            }
        }else
        {
            cache_read(disk_inode->indirect, indirect);
        }


        // TODO figure out i. cant be 0 because need to take into account existing.
        for(i = cur_sectors;
            cur_sectors < DIRECT_COUNT + 128 && cur_sectors < target_sectors; i++)
        {
            if(free_map_allocate(1, &indirect->sectors[i]))
            {
                cache_write(indirect->sectors[i], zeros);
                cur_sectors++;
            }else
            {
                // TODO shrink maybe?
                return false;
            }
        }

        cache_write(disk_inode->indirect, indirect);
        free(indirect);
        cur_sectors = DIRECT_COUNT + 128; // TODO remove , should not be neccesarry
    }



        
    /* Double indirect sectors */
    if(cur_sectors < DIRECT_COUNT + 128 * 128)
    {
        struct inode_disk_list *double_indirect = calloc(1, sizeof(struct inode_disk_list));
        if(double_indirect == NULL)
        {
            // TODO shrink maybe?
            return false;
        }        
        if(disk_inode->double_indirect == 0)
        {             
            if(!free_map_allocate(1, &disk_inode->double_indirect))
            {
                // TODO shrink maybe?
                return false;
            }
        }else
        {
            cache_read(disk_inode->double_indirect, double_indirect);
        }
        for(i = cur_sectors;
            cur_sectors < DIRECT_COUNT + 128 * 128 && cur_sectors < target_sectors;
            i++)
        {
            if(free_map_allocate(1, &indirect->sectors[i]))
            {
                cache_write(indirect->sectors[i], zeros);
                cur_sectors++;
            }else
            {
                // TODO shrink maybe?
                return false;
            }
        }

        cache_write(disk_inode->indirect, indirect);
        free(indirect);
        cur_sectors = DIRECT_COUNT + 128; // TODO remove , should not be neccesarry
    }





    
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t byte_to_sector (const struct inode *inode, off_t pos)
{
    ASSERT (inode != NULL);
    if (pos == -1 || pos >= inode->data.length)
    {
        return -1;
    }
    
    int index = pos / BLOCK_SECTOR_SIZE;
    
    if(index < DIRECT_COUNT)
    {
        /* It is in our direct sectors */
        return inode->data->sectors[index];
    }

    struct inode_disk_list *disk_inode_list;
    if(index < DIRECT_COUNT + 128) 
    {
        /* It is in our indirect sector */
        disk_inode_list = calloc (1, sizeof(struct inode_disk_list));
        buffer_read(inode->data->indirect, (void *) disk_inode_list);
        block_sector_t b = disk_inode_list->sectors[index - DIRECT_COUNT];
        free(disk_inode_list);        
        return b;
    }else if(index < DIRECT_COUNT + 128 * 128)
    {
        /* It is in our double indirect sector */

        /* Doubly indirect */
        disk_inode_list = calloc (1, sizeof(struct inode_disk_list));
        buffer_read(inode->data->double_indirect, (void *) disk_inode_list);
        block_sector_t b = disk_inode_list->sectors[(index - (DIRECT_COUNT + 128) / 128];


        /* Indirect */             
        buffer_read(b, (void *) disk_inode_list);                                                   
        b = disk_inode_list->sectors[(index - (INDIRECT_COUNT + 128)) % 128];
        
        free(disk_inode_list);
        free(disk_inode_list2);
        
        return b;
    }

    return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void inode_init (void)
{
    list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool inode_create (block_sector_t sector, off_t length)
{
    struct inode_disk *disk_inode = NULL;
    bool success = false;

    ASSERT (length >= 0);

    /* If this assertion fails, the inode structure is not exactly
       one sector in size, and you should fix that. */
    ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

    disk_inode = calloc(1, sizeof *disk_inode);
    if(disk_inode != NULL)
    {
        size_t sectors = bytes_to_sectors(length);
        disk_inode->length = length;
        disk_inode->magic = INODE_MAGIC;

        if(allocate_sector(disk_inode, sectors, SECTOR_MAX_LEVEL))
        {
            cache_write(sector, (void *) disk_inode);
            success = true; 
        } 
        
        free(disk_inode);
    }
    return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *inode_open (block_sector_t sector)
{
    struct list_elem *e;
    struct inode *inode;

    /* Check whether this inode is already open. */
    for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
            e = list_next (e))
    {
        inode = list_entry (e, struct inode, elem);
        if (inode->sector == sector)
        {
            inode_reopen (inode);
            return inode;
        }
    }

    /* Allocate memory. */
    inode = malloc (sizeof *inode);
    if (inode == NULL)
        return NULL;

    /* Initialize. */
    list_push_front (&open_inodes, &inode->elem);
    inode->sector = sector;
    inode->open_cnt = 1;
    inode->deny_write_cnt = 0;
    inode->removed = false;
    cache_read(inode->sector, &inode->data);
    return inode;
}

/* Reopens and returns INODE. */
struct inode *inode_reopen (struct inode *inode)
{
    if (inode != NULL)
        inode->open_cnt++;
    return inode;
}

/* Returns INODE's inode number. */
block_sector_t inode_get_inumber (const struct inode *inode)
{
    return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void inode_close (struct inode *inode)
{
    /* Ignore null pointer. */
    if (inode == NULL)
        return;

    /* Release resources if this was the last opener. */
    if (--inode->open_cnt == 0)
    {
        /* Remove from inode list and release lock. */
        list_remove (&inode->elem);

        /* Deallocate blocks if removed. */
        if (inode->removed)
        {
            free_map_release (&inode->sector, 1);
            deallocate_sector(&inode->data, SECTOR_MAX_LEVEL);
        }

        free (inode);
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void inode_remove (struct inode *inode)
{
    ASSERT (inode != NULL);
    inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset)
{
    uint8_t *buffer = buffer_;
    off_t bytes_read = 0;

    if(offset >= inode->data.length)
    {
        return 0;
    }
    
    while (size > 0)
    {
        /* Disk sector to read, starting byte offset within sector. */
        block_sector_t sector_idx = byte_to_sector (inode, offset);
        int sector_ofs = offset % BLOCK_SECTOR_SIZE;

        /* Bytes left in inode, bytes left in sector, lesser of the two. */
        off_t inode_left = inode_length (inode) - offset;
        int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
        int min_left = inode_left < sector_left ? inode_left : sector_left;

        /* Number of bytes to actually copy out of this sector. */
        int chunk_size = size < min_left ? size : min_left;
        if (chunk_size <= 0)
            break;

        if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
            /* Read full sector directly into caller's buffer. */
            cache_read(sector_idx, buffer + bytes_read);
        }
        else
        {            
            cache_read_partial(sector_idx, buffer + bytes_read, sector_ofs, chunk_size);
        }

        /* Advance. */
        size -= chunk_size;
        offset += chunk_size;
        bytes_read += chunk_size;
    }

    return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset)
{
    const uint8_t *buffer = buffer_;
    off_t bytes_written = 0;
    off_t end;
    
    if(inode->deny_write_cnt)
    {
        return 0;
    }

    end = byte_to_sector(inode, offset + size - 1);
    if(end == -1)
    {
        if(allocate_sector(&inode->data,
                           bytes_to_sectors(offset + size),
                           SECTOR_MAX_LEVEL))
        {
            if(offset + size > inode->data.length)
            {
                inode->data.length = offset + size;
            }
            
            cache_write(inode->sector, (void *) &inode->data);
        }
    }
    
    while (size > 0)
    {
        /* Sector to write, starting byte offset within sector. */
        block_sector_t sector_idx = byte_to_sector (inode, offset);
        int sector_ofs = offset % BLOCK_SECTOR_SIZE;

        /* Bytes left in inode, bytes left in sector, lesser of the two. */
        off_t inode_left = inode_length (inode) - offset;
        int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
        int min_left = inode_left < sector_left ? inode_left : sector_left;

        /* Number of bytes to actually write into this sector. */
        int chunk_size = size < min_left ? size : min_left;
        if (chunk_size <= 0)
            break;

        if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
            /* Write full sector directly to disk. */
            cache_write(sector_idx, (void *) buffer + bytes_written);
        }
        else
        {
            if(!(sector_ofs > 0 || chunk_size < sector_left))
            {
                cache_write(sector_idx, 0);
            }
            cache_write_partial(sector_idx,
                                (void *) buffer + bytes_written, sector_ofs, chunk_size);
        }

        /* Advance. */
        size -= chunk_size;
        offset += chunk_size;
        bytes_written += chunk_size;
    }

    return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void inode_deny_write (struct inode *inode)
{
    inode->deny_write_cnt++;
    ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void inode_allow_write (struct inode *inode)
{
    ASSERT (inode->deny_write_cnt > 0);
    ASSERT (inode->deny_write_cnt <= inode->open_cnt);
    inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t inode_length (const struct inode *inode)
{
    return inode->data.length;
}
