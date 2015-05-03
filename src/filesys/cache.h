#include "devices/block.h"
#include <stdbool.h>

void cache_init(void);
void cache_done(void);
int cache_evict(void);
void cache_flush(void);
void cache_read(block_sector_t sector, uint8_t *data);
void cache_read_partial(block_sector_t sector, uint8_t *data, int offset, int chunk_size);
void cache_write(block_sector_t sector, uint8_t *data);
void cache_write_partial(block_sector_t sector, uint8_t *data, int offset, int chunk_size);
void flush_daemon(void *aux);
void read_ahead_daemon(void *aux);
void read_ahead_request(block_sector_t sector);
