#ifndef PMM_H
#define PMM_H
#include <stdint.h>

#define PAGE_SIZE     4096u
/* The PMM manages physical memory below this limit (256 MiB). */
#define PMM_MAX_PHYS  0x10000000u

void     pmm_init(void);                              /* every frame starts "used" */
void     pmm_add_free_region(uint32_t base, uint32_t len);   /* whole frames only */
void     pmm_reserve_region(uint32_t base, uint32_t len);    /* any overlapping frame */
uint32_t pmm_alloc_frame(void);                       /* physical address, 0 on failure */
uint32_t pmm_alloc_contiguous(uint32_t nframes);      /* physical address, 0 on failure */
int      pmm_free_frame(uint32_t phys);               /* 0 ok, -1 invalid/double free */
uint32_t pmm_free_frames(void);
uint32_t pmm_total_frames(void);                      /* frames ever added as usable */
uint32_t pmm_max_usable_addr(void);                   /* end of highest usable region */
#endif
