#include <bitmap.h>
#include "devices/block.h"
#include "threads/vaddr.h"
#include <stdio.h>
#include <string.h>
#include "threads/synch.h"

void swap_init(void);
void swap_read(size_t bitmap_index, void *upage);
size_t swap_write(void *upage);
void swap_remove(int bitmap_index);
