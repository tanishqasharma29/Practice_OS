#ifndef PAGING_H
#define PAGING_H
#include <stdint.h>

#define PAGE_PRESENT 0x1u
#define PAGE_RW      0x2u
#define PAGE_USER    0x4u

/* Identity-map [0, identity_limit) with 4 KiB pages, load CR3, enable paging. */
void paging_init(uint32_t identity_limit);
/* Map one 4 KiB page. Returns 0 on success, -1 if out of frames or bad alignment. */
int  paging_map_page(uint32_t phys, uint32_t virt, uint32_t flags);
/* Unmap one page, returning the physical frame it pointed to (0 if not mapped). */
uint32_t paging_unmap_page(uint32_t virt);
/* Translate: returns 0 and writes *phys if mapped, -1 otherwise. */
int  paging_translate(uint32_t virt, uint32_t *phys);
uint32_t paging_tables_used(void);
#endif
