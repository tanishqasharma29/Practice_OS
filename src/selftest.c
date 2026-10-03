#include "kernel.h"
#include "pmm.h"
#include "paging.h"
#include "kheap.h"
#include "task.h"
#include "demo_tasks.h"

static int fails, checks;
static void check(int ok, const char *what) {
    checks++;
    if (!ok) { fails++; puts("  FAIL: "); puts(what); putc('\n'); }
}

static void test_pmm(void) {
    uint32_t irqf = irq_save();            /* keep other tasks out of the PMM while counting frames */
    uint32_t before = pmm_free_frames();
    uint32_t f[64]; int distinct = 1, aligned = 1;
    for (int i = 0; i < 64; i++) {
        f[i] = pmm_alloc_frame();
        if (!f[i] || (f[i] & 0xFFF)) aligned = 0;
        for (int j = 0; j < i; j++) if (f[i] == f[j]) distinct = 0;
    }
    check(aligned, "pmm frames non-zero and 4K aligned");
    check(distinct, "pmm frames distinct");
    check(pmm_free_frames() == before - 64, "pmm free count drops by 64");
    for (int i = 0; i < 64; i++) pmm_free_frame(f[i]);
    check(pmm_free_frames() == before, "pmm free count restored");
    check(pmm_free_frame(f[0]) == -1, "pmm double free rejected");
    check(pmm_free_frame(0x100000) == -1, "pmm refuses to free kernel frame");
    check(pmm_free_frame(0x1234) == -1, "pmm refuses unaligned free");
    uint32_t c = pmm_alloc_contiguous(16);
    check(c != 0, "pmm contiguous 16 frames");
    if (c) { for (int i = 0; i < 16; i++) pmm_free_frame(c + i * PAGE_SIZE); }
    check(pmm_free_frames() == before, "pmm contiguous freed");
    irq_restore(irqf);
}

static void test_paging(void) {
    uint32_t irqf = irq_save();
    uint32_t virt = 0xE0000000u, phys = pmm_alloc_frame(), got = 0;
    check(phys != 0, "paging got frame");
    check(paging_translate(virt, &got) == -1, "unmapped address not translatable");
    check(paging_map_page(phys, virt, PAGE_RW) == 0, "paging map");
    check(paging_translate(virt + 0x123, &got) == 0 && got == phys + 0x123, "translate keeps offset");
    volatile uint32_t *v = (volatile uint32_t *)virt, *p = (volatile uint32_t *)phys;
    *v = 0xC0FFEE42u;
    check(*p == 0xC0FFEE42u, "write via virtual visible at physical frame");
    check(paging_map_page(phys, virt + 1, PAGE_RW) == -1, "unaligned map rejected");
    check(paging_unmap_page(virt) == phys, "unmap returns frame");
    check(paging_translate(virt, &got) == -1, "unmapped after unmap");
    pmm_free_frame(phys);
    irq_restore(irqf);
}

static void fill(uint8_t *p, size_t n, uint8_t s) { for (size_t i = 0; i < n; i++) p[i] = (uint8_t)(s + i); }
static int verify(uint8_t *p, size_t n, uint8_t s) { for (size_t i = 0; i < n; i++) if (p[i] != (uint8_t)(s + i)) return 0; return 1; }

static void test_heap(void) {
    struct kheap_stats s0, s1;
    kheap_get_stats(&s0);
    check(kheap_check() == 0, "heap consistent at start");
    uint8_t *a = kmalloc(100), *b = kmalloc(200), *c = kmalloc(300);
    check(a && b && c, "kmalloc small blocks");
    check(a && ((uintptr_t)a % 16 == 0) && ((uintptr_t)b % 16 == 0), "16-byte alignment");
    fill(a, 100, 1); fill(b, 200, 2); fill(c, 300, 3);
    check(verify(a, 100, 1) && verify(b, 200, 2) && verify(c, 300, 3), "blocks do not overlap");
    check(kfree(a) == 0 && kfree(c) == 0 && kfree(b) == 0, "free in a, c, b order");
    check(kfree(b) == -1, "double free rejected");
    check(kfree((void *)0x1234) == -1, "wild pointer rejected");
    check(kheap_check() == 0, "heap consistent after frees");
    kheap_get_stats(&s1);
    check(s1.blocks == s0.blocks && s1.used_bytes == s0.used_bytes, "coalescing restored block count");
    size_t before = s1.total;
    uint8_t *big = kmalloc(512 * 1024);               /* forces growth */
    check(big != NULL, "kmalloc 512 KiB grows heap");
    if (big) {
        fill(big, 512 * 1024, 7);
        check(verify(big, 512 * 1024, 7), "big block readable");
        kheap_get_stats(&s1);
        check(s1.total > before, "heap grew");
        kfree(big);
    }
    check(kmalloc(0xF0000000u) == NULL, "absurd size refused");
    void *p[64];
    for (int i = 0; i < 64; i++) p[i] = kmalloc(16 + i * 8);
    for (int i = 0; i < 64; i += 2) kfree(p[i]);
    for (int i = 1; i < 64; i += 2) kfree(p[i]);
    check(kheap_check() == 0, "heap consistent after interleaved frees");
    kheap_get_stats(&s1);
    check(s1.used_bytes == s0.used_bytes && s1.free_blocks == s0.free_blocks, "temporary blocks all coalesced back");
}

static void wait_ticks(uint32_t n) { uint32_t t0 = timer_ticks(); while (timer_ticks() - t0 < n) __asm__ volatile("hlt"); }
static int wait_for(volatile uint32_t *v, uint32_t want, uint32_t max_ticks) {
    uint32_t t0 = timer_ticks();
    while (*v < want && timer_ticks() - t0 < max_ticks) __asm__ volatile("hlt");
    return *v >= want;
}

static void test_tasks(void) {
    task_reap();
    int base = task_count();      /* tasks the user may already have running; the test leaves them alone */ struct kheap_stats s0, s1; kheap_get_stats(&s0);
    check(task_current_id() == 1, "selftest runs on the main task (id 1)");
    check(task_kill(1) == -1 && task_kill(9999) == -1, "cannot kill main or an unknown id");

    /* a task that sleeps, wakes up, finishes and returns from its entry function */
    static struct count_job job; job.n = 5; job.done = 0;
    int id = task_create("count", demo_count_job, &job);
    check(id > 1, "task_create returns a new id");
    check(wait_for(&job.done, 5, 300), "sleeping task wakes 5 times (counter reaches 5)");
    wait_ticks(3);
    check(task_count() == base, "task that returned from its entry function is finished");
    check(task_reap() >= 1, "finished task's stack reaped");

    /* preemption: a task that never yields must not starve main, and main must not starve it */
    demo_spin_count = 0;
    int spin = task_create("spin", demo_spin, 0);
    uint32_t t0 = timer_ticks();
    while (timer_ticks() - t0 < 30) { }                 /* main busy-waits too (no hlt) */
    check(demo_spin_count > 0, "busy task ran while main busy-waited (timer preemption)");
    check(task_kill((uint32_t)spin) == 0, "kill a running task");
    wait_ticks(2);
    uint32_t frozen = demo_spin_count; wait_ticks(5);
    check(demo_spin_count == frozen, "killed task no longer runs");
    check(task_reap() >= 1, "killed task reaped");

    /* heap under concurrent preemption: three tasks and main all hammer kmalloc/kfree */
    demo_errors = 0; demo_churn_done = 0;
    for (int i = 0; i < 3; i++) task_create("churn", demo_churn, (void *)40000);
    uint32_t main_err = 0;
    for (int r = 0; r < 40000; r++) {
        uint8_t *a = kmalloc(100 + r % 200), *b = kmalloc(300);
        if (!a || !b) { main_err++; continue; }
        for (int j = 0; j < 48; j++) a[j] = (uint8_t)(j ^ r);
        for (int j = 0; j < 48; j++) if (a[j] != (uint8_t)(j ^ r)) main_err++;
        if (kfree(a) || kfree(b)) main_err++;
    }
    check(wait_for(&demo_churn_done, 3, 12000), "3 churn tasks finish");
    check(demo_errors == 0 && main_err == 0, "no heap data corruption under preemption");
    check(kheap_check() == 0, "heap chain consistent after concurrent churn");
    wait_ticks(3); task_reap();

    /* task table limits */
    int made = 0, last = 0, ids[MAX_TASKS];
    for (int i = 0; i < MAX_TASKS + 2; i++) { last = task_create("sleeper", demo_count, (void *)1000000); if (last > 0) ids[made++] = last; else break; }
    check(made == MAX_TASKS - base && last < 0, "task table holds exactly MAX_TASKS and then refuses");
    for (int i = 0; i < made; i++) task_kill((uint32_t)ids[i]);
    wait_ticks(2);
    check(task_reap() >= made, "all killed tasks reaped");
    check(task_stack_ok(), "no task stack canary damaged");
    check(task_count() == base, "task count back to what it was");
    kheap_get_stats(&s1);
    check(s1.used_bytes == s0.used_bytes && s1.free_blocks == s0.free_blocks, "task stacks all returned to the heap");
}

int run_selftest(void) {
    fails = checks = 0;
    puts("selftest: pmm\n");    test_pmm();
    puts("selftest: paging\n"); test_paging();
    puts("selftest: heap\n");   test_heap();
    puts("selftest: tasks\n");  test_tasks();
    uint32_t t0 = timer_ticks();
    for (volatile uint32_t i = 0; i < 20000000 && timer_ticks() == t0; i++) { }
    check(timer_ticks() != t0, "timer interrupt advances");
    put_dec(checks - fails); puts("/"); put_dec(checks); puts(" checks passed\n");
    puts(fails ? "SELFTEST FAIL\n" : "SELFTEST PASS\n");
    return fails;
}

