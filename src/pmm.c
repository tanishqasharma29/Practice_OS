#include "pmm.h"

#define NFRAMES (PMM_MAX_PHYS / PAGE_SIZE)

static uint32_t bitmap[NFRAMES / 32];   /* 1 = used or unusable, 0 = free */
static uint32_t owned[NFRAMES / 32];    /* 1 = handed out by the allocator (only these may be freed) */
static uint32_t free_count, total_count, max_addr, hint;

static int  test(uint32_t f)  { return (bitmap[f >> 5] >> (f & 31)) & 1; }
static void set(uint32_t f)   { bitmap[f >> 5] |=  (1u << (f & 31)); }
static void clr(uint32_t f)   { bitmap[f >> 5] &= ~(1u << (f & 31)); }

void pmm_init(void) {
    for (uint32_t i = 0; i < NFRAMES / 32; i++) bitmap[i] = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < NFRAMES / 32; i++) owned[i] = 0;
    free_count = total_count = max_addr = hint = 0;
}

void pmm_add_free_region(uint32_t base, uint32_t len) {
    uint64_t start = ((uint64_t)base + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);
    uint64_t end   = ((uint64_t)base + len) & ~(uint64_t)(PAGE_SIZE - 1);
    if (end > PMM_MAX_PHYS) end = PMM_MAX_PHYS;
    for (uint64_t a = start; a < end; a += PAGE_SIZE) {
        uint32_t f = (uint32_t)(a / PAGE_SIZE);
        if (test(f)) { clr(f); free_count++; total_count++; }
    }
    if (end > start && end > max_addr) max_addr = (uint32_t)end;
}

void pmm_reserve_region(uint32_t base, uint32_t len) {
    uint64_t start = (uint64_t)base & ~(uint64_t)(PAGE_SIZE - 1);
    uint64_t end   = ((uint64_t)base + len + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);
    if (end > PMM_MAX_PHYS) end = PMM_MAX_PHYS;
    for (uint64_t a = start; a < end; a += PAGE_SIZE) {
        uint32_t f = (uint32_t)(a / PAGE_SIZE);
        if (!test(f)) { set(f); free_count--; }
    }
}

uint32_t pmm_alloc_frame(void) {
    if (free_count == 0) return 0;
    for (uint32_t n = 0; n < NFRAMES; n++) {
        uint32_t f = (hint + n) % NFRAMES;
        if (!test(f)) {
            set(f); owned[f >> 5] |= 1u << (f & 31); free_count--; hint = f + 1;
            return f * PAGE_SIZE;
        }
    }
    return 0;
}

uint32_t pmm_alloc_contiguous(uint32_t n) {
    if (n == 0 || n > free_count) return 0;
    uint32_t run = 0;
    for (uint32_t f = 0; f < NFRAMES; f++) {
        run = test(f) ? 0 : run + 1;
        if (run == n) {
            uint32_t first = f - n + 1;
            for (uint32_t i = first; i <= f; i++) { set(i); owned[i >> 5] |= 1u << (i & 31); }
            free_count -= n;
            return first * PAGE_SIZE;
        }
    }
    return 0;
}

int pmm_free_frame(uint32_t phys) {
    if (phys % PAGE_SIZE || phys >= PMM_MAX_PHYS) return -1;
    uint32_t f = phys / PAGE_SIZE;
    if (!((owned[f >> 5] >> (f & 31)) & 1)) return -1;   /* never allocated, or double free */
    owned[f >> 5] &= ~(1u << (f & 31));
    clr(f);
    free_count++;
    if (f < hint) hint = f;
    return 0;
}

uint32_t pmm_free_frames(void)      { return free_count; }
uint32_t pmm_total_frames(void)     { return total_count; }
uint32_t pmm_max_usable_addr(void)  { return max_addr; }
