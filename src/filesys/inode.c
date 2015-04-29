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


/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
{
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    enum inode_type type;               /* FILE or DIR */
    block_sector_t sectors[125];        /* Sectors */
};

/* Indirect and double indirect */
struct inode_disk_list
{
    block_sector_t sectors[128];        /* Sectors */
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

void shrink(struct inode_disk *, off_t length);
bool grow(struct inode_disk *, off_t length);

void shrink(struct inode_disk *disk_inode, off_t length)
{
    int i;
    int j;
    size_t target_sector = bytes_to_sectors(length);
    struct inode_disk_list *indirect;
    struct inode_disk_list *double_indirect;   
    
    /* Doubly indirect sectors */
    if(target_sector < 16635 && disk_inode->sectors[124] != -1)
    {
        double_indirect = disk_inode->sectors[124];
        for(i = 127; i >= 0; i--)
        {
            if(target_sector < 16635 - 128 * (127 - i) &&
               double_indirect->sectors[i] != -1)
            {
                indirect = double_indirect->sectors[i];
                for(j = 127; j >= 0; j--)
                {
                    if(target_sector < 16635 - 128 * (127 - i) - (127 - j) &&
                       indirect->sectors[j] != -1)
                    {
                        free_map_release(indirect->sectors[j], 1);
                        indirect->sectors[j] = -1;
                    }
                }
                if(target_sector < 16635 - 128 * (127 - i) - 127)
                {
                    free_map_release(indirect, 1);
                    double_indirect->sectors[i] = -1;
                }else
                {
                    cache_write(double_indirect->sectors[i], indirect);
                }
            }
        }
        if(target_sector < 252)
        {
            free_map_release(double_indirect, 1);
            disk_inode->sectors[124] = -1;
        }else
        {
            cache_write(disk_inode->sectors[124], double_indirect);
        }
    }
  
    /* Indirect sectors */
    if(target_sector < 251 && disk_inode->sectors[123] != -1)
    {
        indirect = disk_inode->sectors[123];
        for(i = 127; i >= 0; i--)
        {
            if(target_sector < 251 - (127 - i))
            {
                free_map_release(indirect->sectors[i], 1);
                indirect->sectors[i] = -1;
            }
        }
        if(target_sector < 124)
        {
            free_map_release(indirect, 1);
            disk_inode->sectors[123] = -1;
        }
        else
        {
            cache_write(disk_inode->sectors[123], indirect);
        }
    }
  
    /* Direct sectors */
    for(i = 122; i >= 0; i--)
    {
        if(target_sector < 123 - (122 - i) && disk_inode->sectors[i] != -1)
        {
            free_map_release(disk_inode->sectors[i], 1);
            disk_inode->sectors[i] = -1;
        }
    }
  
    disk_inode->length = length;
}

bool grow(struct inode_disk *disk_inode, off_t length)
{
    uint32_t i;
    uint32_t j;
    static char zeros[BLOCK_SECTOR_SIZE];
    size_t target_sectors = bytes_to_sectors(length);
    size_t old_sectors = bytes_to_sectors(disk_inode->length);
    size_t cur_sectors = bytes_to_sectors(disk_inode->length);

    struct inode_disk_list *indirect = calloc(1, sizeof(struct inode_disk_list));
    struct inode_disk_list *double_indirect = calloc(1, sizeof(struct inode_disk_list));
    
    /* Direct sectors */
    if(cur_sectors < 123 && cur_sectors < target_sectors)
    {
        for(i = cur_sectors; i < target_sectors && i < 123; i++)
        {
            if(free_map_allocate(1, &disk_inode->sectors[i]))
            {
                cache_write(disk_inode->sectors[i], zeros);
                cur_sectors++;
            }
            else
            {
                shrink(disk_inode, old_sectors);
                return false;
            }
        }
    }

    /* Indirect sectors */
    if(cur_sectors < 251 && cur_sectors < target_sectors)
    {
        if(disk_inode->sectors[123] == -1)
        {           
            if(free_map_allocate(1, &disk_inode->sectors[123]))
            {
                int k;
                for (k = 0; k < 128; k++)
                {
                    indirect->sectors[k] = -1;
                }
            }else
            {
                shrink(disk_inode, old_sectors);
                return false;
            }
        }else
        {
            cache_read(disk_inode->sectors[123], indirect);
        }
        
        for(i = cur_sectors - 123; i < (target_sectors - 123) && i < 128; i++)
        {
            if(free_map_allocate(1, &indirect->sectors[i]))
            {
                cache_write(indirect->sectors[i], zeros);
                cur_sectors++;
            }
            else
            {
                shrink(disk_inode, old_sectors);
                return false;
            }
        }
        
        cache_write(disk_inode->sectors[123], indirect);        
    }

    /* Doubly indirect blocks */
    if(cur_sectors < 16635 && cur_sectors < target_sectors)
    {
        if(disk_inode->sectors[124] == -1)
        {
            if(free_map_allocate(1, &disk_inode->sectors[124]))
            {
                int k;
                for (k = 0; k < 128; k++)
                {
                    double_indirect->sectors[k] = -1;
                }
            }else
            {
                shrink(disk_inode, old_sectors);
                return false;
            }
        }else
        {
            cache_read(disk_inode->sectors[124], double_indirect);
        }
        
        for(i = (cur_sectors - 251) / 128;
            i < (target_sectors - 251) / 128 + 1 && i < 128; i++)
        {
            if(double_indirect->sectors[i] == -1)
            {
                if(free_map_allocate(1, &double_indirect->sectors[i]))
                {
                    int k;
                    for (k = 0; k < 128; k++)
                    {
                        indirect->sectors[k] = -1;
                    }
                }else
                {
                    shrink(disk_inode, old_sectors);
                    return false;
                }
            }else
            {
                cache_read(double_indirect->sectors[i], indirect);
            }
            for(j = cur_sectors - 251 - (i * 128);
                j < target_sectors - 251 - (i * 128) && j < 128; j++)
            {
                if(free_map_allocate(1, &indirect->sectors[i]))
                {
                    cache_write(indirect->sectors[i], zeros);
                    cur_sectors++;
                }else
                {
                    shrink(disk_inode, old_sectors);
                    return false;
                }
            }
            
            cache_write(double_indirect->sectors[i], indirect);
            
        }
        
        cache_write(disk_inode->sectors[124], double_indirect);
    }

    free(indirect);
    free(double_indirect);
    //printf("cur_sectors: %d\n", cur_sectors);
    //printf("target_sectors: %d\n", target_sectors);
    if(cur_sectors == target_sectors)
    {
        disk_inode->length = length;
        return true;
    }

    return false;
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t byte_to_sector (const struct inode *inode, off_t pos)
{    
    ASSERT(inode != NULL);
    if(pos == -1 || pos >= inode->data.length)
    {
        return -1;
    }

    struct inode_disk_list *i_node_disk;
    int sector = pos / BLOCK_SECTOR_SIZE;

    if(sector < 123) /* It is in our direct sectors */
    {
        return inode->data.sectors[sector];
    }
    if(sector < 251) /* It is in our indirect sector */
    {
        i_node_disk = calloc (1, sizeof(struct inode_disk_list));
        cache_read(inode->data.sectors[123], (void *) i_node_disk);
        block_sector_t b = i_node_disk->sectors[sector - 123];    
        free(i_node_disk);
        return b;
    }
    if(sector < 16635) /* It is in our double indirect sector */
    {
        i_node_disk = calloc (1, sizeof(struct inode_disk_list));
        cache_read(inode->data.sectors[124], (void *) i_node_disk);
        block_sector_t b = i_node_disk->sectors[(sector - 251) / 128];
        cache_read(b, i_node_disk);
        b = i_node_disk->sectors[(sector - 251) % 128];            
        free(i_node_disk);
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
bool inode_create (block_sector_t sector, off_t length, enum inode_type type)
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
        disk_inode->length = 0;
        disk_inode->magic = INODE_MAGIC;
        disk_inode->type = type;

        int i;
        for(i = 0; i < 125; i++)
        {
            disk_inode->sectors[i] = -1;
        }

        success = grow(disk_inode, length);
        if(success)
        {
            cache_write(sector, disk_inode);
        }
        
        free(disk_inode);
        return success;
    }
    
    return false;
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
            free_map_release(inode->sector, 1);
            shrink(&inode->data, 0);
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

        if(sector_idx == -1)
        {            
            return bytes_read;
        }
                
        cache_read_partial(sector_idx, buffer + bytes_read, sector_ofs, chunk_size);

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
    
    if(inode->deny_write_cnt)
    {
        return 0;
    }
    
    /* Grow? */
    if(offset + size > inode->data.length)
    {
        grow(&inode->data, offset + size);
        cache_write(inode->sector, &inode->data);
    }
    
    while (size > 0)
    {
        /* Sector to write, starting byte offset within sector. */
        block_sector_t sector_idx = byte_to_sector (inode, offset);
        ASSERT(sector_idx != -1)
        int sector_ofs = offset % BLOCK_SECTOR_SIZE;

        /* Bytes left in inode, bytes left in sector, lesser of the two. */
        off_t inode_left = inode_length (inode) - offset;
        int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
        int min_left = inode_left < sector_left ? inode_left : sector_left;

        /* Number of bytes to actually write into this sector. */
        int chunk_size = size < min_left ? size : min_left;
        if (chunk_size <= 0)
            break;
                
        cache_write_partial(sector_idx,
                            (void *) buffer + bytes_written, sector_ofs, chunk_size);
        
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

bool inode_is_directory(struct inode *inode)
{
    return inode->data.type == DIR;
}

int inode_number(struct inode *inode)
{
    return (int) inode->sector;
}

int inode_is_removed(struct inode *inode)
{
    return inode->removed;
}
