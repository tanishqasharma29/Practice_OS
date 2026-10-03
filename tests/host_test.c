/* Host-side tests for the pure-logic parts (PMM bitmap, kernel heap). Run with sanitizers. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pmm.h"
#include "kheap.h"
#include "fs.h"

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


/* ---------- StrataFS on a RAM disk, with fault injection ---------- */
#define RSECT 4096
static uint8_t ram[RSECT * 512]; static int fail_after = -1, writes_done;
static int r_read(uint32_t l, void *b) { if (l >= RSECT) return -1; memcpy(b, ram + l * 512, 512); return 0; }
static int r_write(uint32_t l, const void *b) {
    if (l >= RSECT) return -1;
    if (fail_after >= 0 && writes_done >= fail_after) return -1;
    writes_done++; memcpy(ram + l * 512, b, 512); return 0;
}
static struct fs_dev rdev = { r_read, r_write, RSECT };
static int lcount, ldirs; static char lnames[80][40];
static void lcb(void *c, const char *n, int d, uint32_t sz) { (void)c; (void)sz; ldirs += d; if (lcount < 80) strcpy(lnames[lcount], n); lcount++; }
static int has(const char *n) { for (int i = 0; i < lcount && i < 80; i++) if (!strcmp(lnames[i], n)) return 1; return 0; }
static int count_in(const char *path) { lcount = ldirs = 0; int r = fs_list(path, lcb, 0); return r ? -1 : lcount; }

static void test_fs(void) {
    uint8_t buf[70000], rd[70000]; uint32_t n; char cwd[200];
    memset(ram, 0, sizeof ram);
    CHECK(fs_mount(&rdev) == FS_ENOFS);                       /* blank disk is not formatted behind your back */
    { uint32_t sum = 0; for (size_t i = 0; i < sizeof ram; i++) sum += ram[i]; CHECK(sum == 0); }
    CHECK(fs_write("x", "a", 1) == FS_ENOTMOUNTED);
    CHECK(fs_format(&rdev) == FS_OK && fs_mount(&rdev) == FS_OK);
    CHECK(count_in("/") == 0);

    CHECK(fs_write("hello.txt", "hello", 5) == FS_OK);
    CHECK(fs_read("hello.txt", rd, sizeof rd, &n) == FS_OK && n == 5 && !memcmp(rd, "hello", 5));
    CHECK(fs_write("hello.txt", "bye!", 4) == FS_OK);          /* replace */
    CHECK(fs_read("/hello.txt", rd, sizeof rd, &n) == FS_OK && n == 4 && !memcmp(rd, "bye!", 4));
    CHECK(fs_write("empty", "", 0) == FS_OK && fs_read("empty", rd, 10, &n) == FS_OK && n == 0);
    CHECK(fs_read("hello.txt", rd, 3, &n) == FS_ETOOBIG);
    CHECK(fs_read("nope", rd, 10, &n) == FS_ENOENT);

    CHECK(fs_mkdir("docs") == FS_OK && fs_mkdir("docs") == FS_EEXIST && fs_mkdir("hello.txt") == FS_EEXIST);
    CHECK(fs_mkdir("docs/sub") == FS_OK && fs_mkdir("nodir/x") == FS_ENOENT && fs_mkdir("hello.txt/x") == FS_ENOTDIR);
    CHECK(fs_write("docs/sub/a.txt", "deep", 4) == FS_OK);
    CHECK(fs_write("docs/hello.txt", "same name other dir", 19) == FS_OK);
    CHECK(fs_read("hello.txt", rd, 10, &n) == FS_OK && n == 4);
    CHECK(count_in("/") == 3 && has("docs") && has("hello.txt") && has("empty") && ldirs == 1);
    CHECK(count_in("docs") == 2 && count_in("/docs/sub/") == 1 && has("a.txt"));
    CHECK(fs_write("docs", "x", 1) == FS_EISDIR && fs_read("docs", rd, 10, &n) == FS_EISDIR);
    CHECK(fs_chdir("docs/sub") == FS_OK && fs_getcwd(cwd, sizeof cwd) == FS_OK && !strcmp(cwd, "/docs/sub"));
    CHECK(fs_read("a.txt", rd, 10, &n) == FS_OK && n == 4 && fs_read("../hello.txt", rd, 30, &n) == FS_OK && n == 19);
    CHECK(fs_read("../../hello.txt", rd, 30, &n) == FS_OK && n == 4);
    CHECK(fs_read("../../../../hello.txt", rd, 30, &n) == FS_OK);     /* .. at root stays at root */
    CHECK(fs_remove("/docs") == FS_ENOTEMPTY);
    CHECK(fs_chdir("a.txt") == FS_ENOTDIR && fs_chdir("zzz") == FS_ENOENT);
    CHECK(fs_chdir("/") == FS_OK && fs_getcwd(cwd, sizeof cwd) == FS_OK && !strcmp(cwd, "/"));
    CHECK(fs_remove("docs/sub/a.txt") == FS_OK && fs_remove("docs/sub") == FS_OK && fs_remove("docs/sub") == FS_ENOENT);
    CHECK(fs_chdir("docs") == FS_OK && fs_remove("/docs/hello.txt") == FS_OK && fs_remove("/docs") == FS_EBUSY);
    CHECK(fs_chdir("/") == FS_OK && fs_remove("docs") == FS_OK && fs_remove("/") == FS_EBUSY);

    /* name and path validation */
    CHECK(fs_write("a b", "x", 1) == FS_EINVAL && fs_write("", "x", 1) == FS_EINVAL && fs_write("/", "x", 1) == FS_EINVAL);
    CHECK(fs_write("..", "x", 1) == FS_EINVAL && fs_write("a/..", "x", 1) != FS_OK);
    char longn[64]; memset(longn, 'n', 32); longn[32] = 0; CHECK(fs_write(longn, "x", 1) == FS_EINVAL);
    longn[31] = 0; CHECK(fs_write(longn, "x", 1) == FS_OK);   /* exactly 31 chars */
    CHECK(fs_remove(longn) == FS_OK);

    /* multi-sector contents, replace with larger/smaller, boundaries */
    uint32_t sizes[] = {1, 511, 512, 513, 1024, 5000, 65536};
    for (unsigned k = 0; k < sizeof sizes / sizeof *sizes; k++) {
        for (uint32_t i = 0; i < sizes[k]; i++) buf[i] = (uint8_t)(i * 31 + k);
        CHECK(fs_write("big", buf, sizes[k]) == FS_OK);
        memset(rd, 0, sizeof rd);
        CHECK(fs_read("big", rd, sizeof rd, &n) == FS_OK && n == sizes[k] && !memcmp(rd, buf, n));
    }
    CHECK(fs_write("toobig", buf, 65537) == FS_ETOOBIG);

    /* persistence: remount from the same bytes, check unchanged */
    CHECK(fs_mount(&rdev) == FS_OK && fs_read("big", rd, sizeof rd, &n) == FS_OK && n == 65536 && !memcmp(rd, buf, n));
    CHECK(fs_read("hello.txt", rd, 10, &n) == FS_OK && n == 4 && !memcmp(rd, "bye!", 4));

    /* directory table full */
    int made = 0;
    for (int i = 0; i < 80; i++) { char nm[16]; sprintf(nm, "f%d", i); int r = fs_write(nm, "z", 1); if (r == FS_OK) made++; else { CHECK(r == FS_ENOSPC); break; } }
    CHECK(made > 40 && made < 64);
    CHECK(fs_write("hello.txt", "still replaceable", 17) == FS_OK);   /* replacing needs no new entry */
    CHECK(fs_mkdir("one-more") == FS_ENOSPC);
    for (int i = 0; i < made; i++) { char nm[16]; sprintf(nm, "f%d", i); CHECK(fs_remove(nm) == FS_OK); }

    /* disk full: contiguous allocation fails cleanly and keeps old data */
    for (uint32_t i = 0; i < 65536; i++) buf[i] = (uint8_t)i;
    int files = 0;
    for (int i = 0; i < 40; i++) { char nm[16]; sprintf(nm, "g%d", i); int r = fs_write(nm, buf, 65536); if (r) { CHECK(r == FS_ENOSPC); break; } files++; }
    CHECK(files >= 1 && files < 40);
    CHECK(fs_write("big", buf, 65536) == FS_OK || 1);              /* may or may not fit; must not crash */
    CHECK(fs_read("g0", rd, sizeof rd, &n) == FS_OK && n == 65536 && !memcmp(rd, buf, n));
    for (int i = 0; i < files; i++) { char nm[16]; sprintf(nm, "g%d", i); CHECK(fs_remove(nm) == FS_OK); }
    uint32_t f, d, fr, tot; fs_usage(&f, &d, &fr, &tot);
    CHECK(tot == RSECT - 9 && fr <= tot && f >= 1);

    /* I/O failure injection at every write point of a replace: old content or new content, never garbage */
    fs_remove("big");
    CHECK(fs_write("v", "old-old-old", 11) == FS_OK);
    for (int cut = 0; cut < 4; cut++) {
        fail_after = writes_done + cut;
        int r = fs_write("v", buf, 1500);                         /* 3 data sectors + 1 dir sector = 4 writes */
        fail_after = -1;
        CHECK(r == (cut < 4 ? FS_EIO : FS_OK));
        CHECK(fs_read("v", rd, sizeof rd, &n) == FS_OK && n == 11 && !memcmp(rd, "old-old-old", 11));   /* in-memory view intact */
        CHECK(fs_mount(&rdev) == FS_OK && fs_read("v", rd, sizeof rd, &n) == FS_OK && n == 11 && !memcmp(rd, "old-old-old", 11));
    }
    fail_after = writes_done;
    CHECK(fs_mkdir("zz") == FS_EIO); fail_after = -1;
    CHECK(fs_stat("zz", &(struct fs_stat){0}) == FS_ENOENT);       /* failed mkdir left nothing behind */
    CHECK(fs_mount(&rdev) == FS_OK && fs_stat("zz", &(struct fs_stat){0}) == FS_ENOENT);
    fail_after = writes_done; CHECK(fs_remove("v") == FS_EIO); fail_after = -1;
    CHECK(fs_read("v", rd, sizeof rd, &n) == FS_OK && n == 11);

    /* corrupt metadata is refused, never repaired or overwritten */
    CHECK(fs_write("c1", "abc", 3) == FS_OK && fs_write("c2", "def", 3) == FS_OK);
    uint8_t save[512 * 9]; memcpy(save, ram, sizeof save);
    for (int i = 0; i < 64; i++) {                                 /* every flipped byte of the dir table either mounts consistently or is refused */
        for (int off = 0; off < 64; off += 7) {
            memcpy(ram, save, sizeof save);
            ram[512 + i * 64 + off] ^= 0xA5;
            uint8_t before[512 * 9]; memcpy(before, ram, sizeof before);
            int r = fs_mount(&rdev);
            CHECK(r == FS_OK || r == FS_ECORRUPT);
            CHECK(!memcmp(before, ram, sizeof before));            /* mount never writes */
            if (r == FS_OK) fs_read("c1", rd, sizeof rd, &n);      /* must not crash */
        }
    }
    memcpy(ram, save, sizeof save);
    ram[512 + 64 * 0 + 32] = 7;                                    /* invalid type */
    CHECK(fs_mount(&rdev) == FS_ECORRUPT && !fs_is_mounted());
    memcpy(ram, save, sizeof save); ram[0] = 'X';
    CHECK(fs_mount(&rdev) == FS_ENOFS);
    memcpy(ram, save, sizeof save);
    CHECK(fs_mount(&rdev) == FS_OK);
}

int main(void) {
    test_pmm(); test_heap(); test_fs();
    printf("%d/%d host checks passed\n", checks - fails, checks);
    return fails != 0;
}
