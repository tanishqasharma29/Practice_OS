#include "paging.h"
#include "pmm.h"

#define ENTRIES 1024
static uint32_t *page_dir;
static uint32_t tables_used;
static int paging_on;

static void zero_page(uint32_t *p) { for (int i = 0; i < ENTRIES; i++) p[i] = 0; }
static void invlpg(uint32_t v) { if (paging_on) __asm__ volatile("invlpg (%0)" :: "r"(v) : "memory"); }

int paging_map_page(uint32_t phys, uint32_t virt, uint32_t flags) {
    if ((phys | virt) & 0xFFFu) return -1;
    uint32_t di = virt >> 22, ti = (virt >> 12) & 0x3FF;
    if (!(page_dir[di] & PAGE_PRESENT)) {
        uint32_t t = pmm_alloc_frame();
        if (!t) return -1;
        zero_page((uint32_t *)t);
        tables_used++;
        /* directory entries are permissive; the page table entry decides access */
        page_dir[di] = t | PAGE_PRESENT | PAGE_RW | PAGE_USER;
    }
    uint32_t *pt = (uint32_t *)(page_dir[di] & ~0xFFFu);
    pt[ti] = phys | (flags & 0xFFFu) | PAGE_PRESENT;
    invlpg(virt);
    return 0;
}

uint32_t paging_unmap_page(uint32_t virt) {
    uint32_t di = virt >> 22, ti = (virt >> 12) & 0x3FF;
    if (!(page_dir[di] & PAGE_PRESENT)) return 0;
    uint32_t *pt = (uint32_t *)(page_dir[di] & ~0xFFFu);
    if (!(pt[ti] & PAGE_PRESENT)) return 0;
    uint32_t phys = pt[ti] & ~0xFFFu;
    pt[ti] = 0;
    invlpg(virt);
    return phys;
}

int paging_translate(uint32_t virt, uint32_t *phys) {
    uint32_t di = virt >> 22, ti = (virt >> 12) & 0x3FF;
    if (!(page_dir[di] & PAGE_PRESENT)) return -1;
    uint32_t *pt = (uint32_t *)(page_dir[di] & ~0xFFFu);
    if (!(pt[ti] & PAGE_PRESENT)) return -1;
    if (phys) *phys = (pt[ti] & ~0xFFFu) | (virt & 0xFFFu);
    return 0;
}

uint32_t paging_tables_used(void) { return tables_used; }

void paging_init(uint32_t identity_limit) {
    page_dir = (uint32_t *)pmm_alloc_frame();
    zero_page(page_dir);
    for (uint32_t a = 0; a < identity_limit; a += PAGE_SIZE)
        paging_map_page(a, a, PAGE_RW);
    __asm__ volatile("mov %0, %%cr3" :: "r"(page_dir) : "memory");
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000u | 0x10000u;       /* PG + WP (kernel honours read-only pages) */
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0) : "memory");
    paging_on = 1;
}
