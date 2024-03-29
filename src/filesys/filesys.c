#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/cache.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void filesys_init (bool format)
{
    fs_device = block_get_role (BLOCK_FILESYS);
    if (fs_device == NULL)
        PANIC ("No file system device found, can't initialize file system.");

    inode_init ();
    free_map_init ();
    cache_init();
    if (format)
        do_format ();
    
    free_map_open ();
    
    /* Start flush daemon */
    thread_create("flush daemon", 63, flush_daemon, NULL);

    /* Start readahead daemon */
    thread_create("readahead daemon", 63, read_ahead_daemon, NULL);
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void filesys_done (void)
{
    free_map_close ();
    cache_done();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool filesys_create (const char *name, off_t initial_size)
{
    block_sector_t inode_sector = 0;
    struct dir *dir = get_dir(name, true);
    char *file = get_filename(name);
    bool success = (dir != NULL
                    && free_map_allocate (1, &inode_sector)
                    && inode_create (inode_sector, initial_size, FILE)
                    && dir_add (dir, file, inode_sector));
    if (!success && inode_sector != 0)
        free_map_release (inode_sector, 1);
    dir_close (dir);

    return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *filesys_open (const char *name)
{
    struct dir *dir = get_dir(name, true);
    struct inode *inode = NULL;
    char *file = get_filename(name);
    if(*file == '\0')
    {
        inode = dir_get_inode(dir);
    }else
    {
        if(dir != NULL)
        {
            dir_lookup(dir, file, &inode);
        }
        dir_close (dir);
    }

    if(inode == NULL)
    {
        return NULL;
    }else
    {
        return file_open (inode);
    }
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool filesys_remove (const char *name)
{
    struct dir *dir = get_dir(name, true);
    char *file = get_filename(name);
    bool success = dir != NULL && dir_remove (dir, file);
    dir_close (dir);

    return success;
}

/* Formats the file system. */
static void do_format (void)
{
    printf ("Formatting file system...");
    free_map_create ();
    if (!dir_create (ROOT_DIR_SECTOR, 16, NULL))
        PANIC ("root directory creation failed");
    free_map_close ();
    printf ("done.\n");
}
