#include "threads/palloc.h"
#include "threads/synch.h"
#include "vm/page.h"
#include <stdint.h>
#include <list.h>
#include <stdbool.h>

void frame_table_init(void);
void *frame_alloc(enum palloc_flags flags, const void *uadd);
void *vm_frame_free(void *frame);
void *vm_frame_evict(const void *uaddr);
struct frame *vm_frame_pick_victim(void);

struct frame
{
    void *kpage;
    struct list_elem elem;
    struct thread *thread;
    void *uaddr;
};
