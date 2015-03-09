#include "threads/palloc.h"
#include "threads/synch.h"
#include "vm/page.h"
#include <stdint.h>
#include <list.h>
#include <stdbool.h>


void frame_table_init(void);
void *frame_alloc(enum palloc_flags flags);
void *vm_frame_free(void *frame);


struct frame
{
    void *kpage;
    struct list_elem elem;
    struct thread *thread;
};
