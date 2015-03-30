#include "threads/palloc.h"
#include "threads/synch.h"
#include "vm/page.h"
#include <stdint.h>
#include <list.h>
#include <stdbool.h>

void frame_table_init(void);
void *frame_alloc(enum palloc_flags flags, struct spt_entry *spte);
void frame_free(void *frame);
void *frame_evict(enum palloc_flags flags);
struct frame_entry *frame_pick_victim(void);

struct frame_entry
{
    void *kpage;
    struct list_elem elem;
    struct thread *thread;
    struct spt_entry *spte;
};
