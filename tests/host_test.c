/* Host-side tests for the pure-logic parts (PMM bitmap, kernel heap). Run with sanitizers. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pmm.h"
#include "kheap.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static uint8_t *arena; static size_t arena_used;
static int grow(void *addr, size_t bytes) {
    if ((uint8_t *)addr != arena + arena_used || arena_used + bytes > (size_t)32 << 20) return -1;
    arena_used += bytes; return 0;
}

static void test_pmm(void) {
    pmm_init();
    pmm_add_free_region(0, 0x9F000);
    pmm_add_free_region(0x100000, 0x700000);          /* 7 MiB */
    pmm_reserve_region(0, 0x100000);
    pmm_reserve_region(0x100000, 0x10001);            /* partial frame rounds outward: 17 frames */
    CHECK(pmm_total_frames() == 0x9F + 0x700);
    CHECK(pmm_free_frames() == 0x700 - 17);
    uint32_t a = pmm_alloc_frame();
    CHECK(a == 0x111000);
    CHECK(pmm_free_frame(a) == 0);
    CHECK(pmm_free_frame(a) == -1);                   /* double free */
    CHECK(pmm_free_frame(0x100000) == -1);            /* reserved, never allocated */
    CHECK(pmm_free_frame(0x5000) == -1);              /* below 1 MiB, reserved */
    CHECK(pmm_free_frame(0x1001) == -1);
    CHECK(pmm_free_frame(0xFFFFF000u) == -1);
    uint32_t c = pmm_alloc_contiguous(32);
    CHECK(c && c % PAGE_SIZE == 0);
    uint32_t free_mid = pmm_free_frames();
    for (uint32_t i = 0; i < 32; i++) CHECK(pmm_free_frame(c + i * PAGE_SIZE) == 0);
    CHECK(pmm_free_frames() == free_mid + 32);
    CHECK(pmm_alloc_contiguous(0x10000) == 0);        /* more than exists */
    uint32_t n = pmm_free_frames(), got = 0, last = 0;
    while ((last = pmm_alloc_frame())) got++;
    CHECK(got == n && pmm_free_frames() == 0);        /* exhaustion returns 0 cleanly */
    CHECK(pmm_alloc_frame() == 0);
}

static void test_heap(void) {
    arena = aligned_alloc(4096, (size_t)32 << 20); arena_used = 0;
    kheap_init(arena, 256 * 1024, (size_t)32 << 20, grow);
    CHECK(kheap_check() == 0);
    struct kheap_stats s; kheap_get_stats(&s);
    CHECK(s.blocks == 1 && s.free_blocks == 1 && s.used_bytes == 0);

    void *a = kmalloc(1), *b = kmalloc(100), *c = kmalloc(5000);
    CHECK(a && b && c && (uintptr_t)a % 16 == 0 && (uintptr_t)b % 16 == 0);
    CHECK(kfree(b) == 0); CHECK(kfree(b) == -1);      /* double free */
    CHECK(kfree((uint8_t *)a + 1) == -1);             /* interior pointer */
    CHECK(kfree(NULL) == 0);
    CHECK(kfree(a) == 0); CHECK(kfree(c) == 0);
    kheap_get_stats(&s);
    CHECK(s.blocks == 1 && s.used_bytes == 0 && kheap_check() == 0);

    CHECK(kmalloc(0) == NULL);
    CHECK(kmalloc((size_t)64 << 20) == NULL);          /* beyond max */
    void *big = kmalloc(10 << 20);                     /* forces repeated growth */
    CHECK(big != NULL); memset(big, 0xAB, 10 << 20);
    CHECK(kheap_check() == 0); CHECK(kfree(big) == 0);

    /* randomized stress: 10000 ops with content verification */
    enum { N = 128 };
    void *p[N] = {0}; size_t sz[N] = {0}; uint8_t pat[N] = {0};
    srand(12345);
    for (int op = 0; op < 10000; op++) {
        int i = rand() % N;
        if (p[i]) {
            for (size_t k = 0; k < sz[i]; k++) if (((uint8_t *)p[i])[k] != (uint8_t)(pat[i] + k)) { CHECK(0 && "corruption"); break; }
            CHECK(kfree(p[i]) == 0); p[i] = NULL;
        } else {
            sz[i] = 1 + rand() % 3000; p[i] = kmalloc(sz[i]); pat[i] = (uint8_t)rand();
            CHECK(p[i] != NULL && (uintptr_t)p[i] % 16 == 0);
            for (size_t k = 0; k < sz[i]; k++) ((uint8_t *)p[i])[k] = (uint8_t)(pat[i] + k);
        }
        if (op % 500 == 0) CHECK(kheap_check() == 0);
    }
    for (int i = 0; i < N; i++) if (p[i]) CHECK(kfree(p[i]) == 0);
    kheap_get_stats(&s);
    CHECK(kheap_check() == 0 && s.used_bytes == 0 && s.free_blocks == 1);   /* fully coalesced */
    free(arena);
}

int main(void) {
    test_pmm(); test_heap();
    printf("%d/%d host checks passed\n", checks - fails, checks);
    return fails != 0;
}
