#ifndef KERNEL_H
#define KERNEL_H
#include <stddef.h>
#include <stdint.h>

void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
int   memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int   streq(const char *a, const char *b);
int   parse_num(const char *s, uint32_t *out);   /* decimal or 0x hex */

void putc(char c);
void puts(const char *s);
void put_dec(uint32_t v);
void put_hex(uint32_t v);
void screen_clear(void);

static inline void outb(uint16_t p, uint8_t v) { __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
static inline uint8_t inb(uint16_t p) { uint8_t v; __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }

static inline uint32_t irq_save(void) {
    uint32_t f; __asm__ volatile("pushf; pop %0; cli" : "=r"(f) :: "memory"); return f;
}
static inline void irq_restore(uint32_t f) {
    __asm__ volatile("push %0; popf" :: "r"(f) : "memory", "cc");
}

void tss_set_stack(uint32_t esp);
uint32_t timer_ticks(void);
void qemu_exit(uint8_t code);      /* isa-debug-exit; no-op on real hardware */
int  run_selftest(void);           /* returns number of failed checks */
#endif
