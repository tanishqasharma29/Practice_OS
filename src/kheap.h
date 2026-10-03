#ifndef KHEAP_H
#define KHEAP_H
#include <stddef.h>
#include <stdint.h>

/* grow(addr, bytes): make [addr, addr+bytes) usable memory, return 0 on success. */
typedef int (*kheap_grow_fn)(void *addr, size_t bytes);

void   kheap_init(void *base, size_t initial_bytes, size_t max_bytes, kheap_grow_fn grow);
void  *kmalloc(size_t size);          /* 16-byte aligned, NULL on failure */
int    kfree(void *ptr);              /* 0 ok, -1 invalid pointer or double free */
int    kheap_check(void);             /* 0 = block chain consistent, -1 = corrupt */

struct kheap_stats { size_t total, used_bytes, free_bytes, largest_free, blocks, free_blocks; };
void   kheap_get_stats(struct kheap_stats *s);
#endif
