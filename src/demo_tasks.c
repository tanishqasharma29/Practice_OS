#include "demo_tasks.h"
#include "kernel.h"
#include "kheap.h"
#include "task.h"

volatile uint32_t demo_spin_count;
volatile uint32_t demo_counter, demo_errors, demo_churn_done;

void demo_blink(void *arg) {
    (void)arg;
    volatile uint16_t *vga = (volatile uint16_t *)0xB8000;
    for (int state = 0;; state ^= 1) {
        vga[79] = (uint16_t)((0x0A << 8) | (state ? 'T' : 't'));
        task_sleep(25);
    }
}

void demo_count(void *arg) {
    for (uint32_t n = (uint32_t)(uintptr_t)arg; n; n--) { task_sleep(10); demo_counter++; }
}

void demo_churn(void *arg) {
    uint32_t rounds = (uint32_t)(uintptr_t)arg, seed = timer_ticks() * 2654435761u + task_current_id();
    for (uint32_t r = 0; r < rounds; r++) {
        uint8_t *p[6]; uint32_t sz[6];
        for (int i = 0; i < 6; i++) {
            seed = seed * 1664525u + 1013904223u;
            sz[i] = 64 + (seed >> 16) % 600;
            p[i] = kmalloc(sz[i]);
            if (!p[i]) { demo_errors++; continue; }
            for (uint32_t j = 0; j < 48; j++) p[i][j] = (uint8_t)(j + i + r);
        }
        for (int i = 0; i < 6; i++) {
            if (!p[i]) continue;
            for (uint32_t j = 0; j < 48; j++) if (p[i][j] != (uint8_t)(j + i + r)) { demo_errors++; break; }
            if (kfree(p[i]) != 0) demo_errors++;
        }
    }
    demo_churn_done++;
}

void demo_spin(void *arg) { (void)arg; for (;;) demo_spin_count++; }

void demo_count_job(void *arg) {
    struct count_job *j = arg;
    for (uint32_t n = j->n; n; n--) { task_sleep(10); j->done++; }
}
