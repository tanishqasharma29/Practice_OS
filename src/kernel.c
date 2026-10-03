/* StrataOS - small educational 32-bit x86 kernel. Phase 1: memory management. */
#include "kernel.h"
#include "pmm.h"
#include "paging.h"
#include "kheap.h"

/* ---------- console: VGA text + COM1 serial ---------- */
#define COM1 0x3F8
static volatile uint16_t *const vga = (volatile uint16_t *)0xB8000;
static int cx, cy;

static void serial_init(void) {
    outb(COM1 + 1, 0); outb(COM1 + 3, 0x80); outb(COM1, 1); outb(COM1 + 1, 0);
    outb(COM1 + 3, 0x03); outb(COM1 + 2, 0xC7); outb(COM1 + 4, 0x0B);
}
static void serial_putc(char c) {
    for (int i = 0; i < 100000 && !(inb(COM1 + 5) & 0x20); i++) { }
    outb(COM1, (uint8_t)c);
}
static void vga_scroll(void) {
    for (int i = 0; i < 80 * 24; i++) vga[i] = vga[i + 80];
    for (int i = 80 * 24; i < 80 * 25; i++) vga[i] = 0x0F20;
    cy = 24;
}
void screen_clear(void) {
    for (int i = 0; i < 80 * 25; i++) vga[i] = 0x0F20;
    cx = cy = 0;
}
void putc(char c) {
    if (c == '\n') serial_putc('\r');
    serial_putc(c);
    if (c == '\n') { cx = 0; cy++; }
    else if (c == '\b') { if (cx > 0) { cx--; vga[cy * 80 + cx] = 0x0F20; } }
    else { vga[cy * 80 + cx] = 0x0F00 | (uint8_t)c; if (++cx == 80) { cx = 0; cy++; } }
    if (cy >= 25) vga_scroll();
}
void puts(const char *s) { while (*s) putc(*s++); }
void put_dec(uint32_t v) {
    char b[11]; int i = 10; b[i] = 0;
    do { b[--i] = '0' + v % 10; v /= 10; } while (v);
    puts(&b[i]);
}
void put_hex(uint32_t v) {
    puts("0x");
    for (int i = 28; i >= 0; i -= 4) putc("0123456789abcdef"[(v >> i) & 0xF]);
}
void qemu_exit(uint8_t code) { outb(0xF4, code); }

/* ---------- GDT ---------- */
struct gdt_entry { uint16_t lim, base_lo; uint8_t base_mid, access, gran, base_hi; } __attribute__((packed));
struct dt_ptr { uint16_t limit; uint32_t base; } __attribute__((packed));
static struct gdt_entry gdt[3];
static void gdt_set(int i, uint8_t access, uint8_t gran) {
    gdt[i].lim = 0xFFFF; gdt[i].base_lo = 0; gdt[i].base_mid = 0; gdt[i].base_hi = 0;
    gdt[i].access = access; gdt[i].gran = gran;
}
static void gdt_init(void) {
    gdt_set(1, 0x9A, 0xCF); gdt_set(2, 0x92, 0xCF);          /* flat ring-0 code and data */
    struct dt_ptr p = { sizeof gdt - 1, (uint32_t)gdt };
    __asm__ volatile("lgdt %0\n ljmp $0x08, $1f\n 1:\n"
                     "mov $0x10, %%ax\n mov %%ax, %%ds\n mov %%ax, %%es\n"
                     "mov %%ax, %%fs\n mov %%ax, %%gs\n mov %%ax, %%ss\n" :: "m"(p) : "eax", "memory");
}

/* ---------- IDT, PIC, PIT, keyboard ---------- */
struct idt_entry { uint16_t lo, sel; uint8_t zero, flags; uint16_t hi; } __attribute__((packed));
struct regs { uint32_t edi, esi, ebp, esp_unused, ebx, edx, ecx, eax, int_no, err, eip, cs, eflags; };
extern uint32_t isr_stub_table[48];
static struct idt_entry idt[256];

static void idt_init(void) {
    for (int i = 0; i < 48; i++) {
        uint32_t a = isr_stub_table[i];
        idt[i].lo = a & 0xFFFF; idt[i].hi = a >> 16; idt[i].sel = 0x08; idt[i].zero = 0; idt[i].flags = 0x8E;
    }
    struct dt_ptr p = { sizeof idt - 1, (uint32_t)idt };
    __asm__ volatile("lidt %0" :: "m"(p));
}
static void pic_init(void) {
    outb(0x20, 0x11); outb(0xA0, 0x11); outb(0x21, 32); outb(0xA1, 40);
    outb(0x21, 4); outb(0xA1, 2); outb(0x21, 1); outb(0xA1, 1);
    outb(0x21, 0xFC); outb(0xA1, 0xFF);                     /* only IRQ0 timer + IRQ1 keyboard */
}
static volatile uint32_t ticks;
uint32_t timer_ticks(void) { return ticks; }
static void timer_init(void) {
    uint32_t div = 1193182 / 100;                           /* 100 Hz */
    outb(0x43, 0x36); outb(0x40, div & 0xFF); outb(0x40, div >> 8);
}

static char kbuf[64]; static volatile int khead, ktail;
static const char keymap[128] = {
    0,27,'1','2','3','4','5','6','7','8','9','0','-','=','\b','\t',
    'q','w','e','r','t','y','u','i','o','p','[',']','\n',0,'a','s',
    'd','f','g','h','j','k','l',';','\'','`',0,'\\','z','x','c','v',
    'b','n','m',',','.','/',0,'*',0,' '
};
static void kbd_irq(void) {
    uint8_t sc = inb(0x60);
    if (sc & 0x80 || sc >= 128) return;
    char c = keymap[sc];
    if (c && ((khead + 1) % 64) != ktail) { kbuf[khead] = c; khead = (khead + 1) % 64; }
}

static void panic_exit(const char *msg, struct regs *r, uint8_t code) {
    puts("\n*** "); puts(msg);
    puts(" at eip="); put_hex(r->eip); puts(" err="); put_hex(r->err); puts("\n");
    qemu_exit(code);
    for (;;) __asm__ volatile("cli; hlt");
}

void isr_handler(struct regs *r) {
    if (r->int_no == 14) {
        uint32_t cr2; __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        puts("\n*** PAGE FAULT accessing "); put_hex(cr2);
        puts(" err="); put_hex(r->err); puts(" eip="); put_hex(r->eip); puts("\n");
        qemu_exit(0x11);                                    /* QEMU exit status 35 */
        for (;;) __asm__ volatile("cli; hlt");
    }
    if (r->int_no < 32) panic_exit("CPU EXCEPTION", r, 0x12);
    if (r->int_no == 32) ticks++;
    else if (r->int_no == 33) kbd_irq();
    outb(0x20, 0x20);                                       /* EOI (only IRQs 0-1 are unmasked) */
}

/* ---------- multiboot ---------- */
struct mb_info {
    uint32_t flags, mem_lower, mem_upper, boot_device, cmdline, mods_count, mods_addr;
    uint32_t syms[4], mmap_length, mmap_addr;
} __attribute__((packed));
struct mb_mmap { uint32_t size; uint64_t addr, len; uint32_t type; } __attribute__((packed));
extern uint8_t kernel_start, kernel_end;

#define HEAP_VIRT 0xD0000000u
#define HEAP_MAX  (16u * 1024 * 1024)
static uint32_t heap_mapped;
static int heap_grow(void *addr, size_t bytes) {
    uint32_t base = (uint32_t)addr, done = 0;
    for (; done < bytes; done += PAGE_SIZE) {
        uint32_t f = pmm_alloc_frame();
        if (!f || paging_map_page(f, base + done, PAGE_RW) != 0) {
            if (f) pmm_free_frame(f);
            while (done) { done -= PAGE_SIZE; pmm_free_frame(paging_unmap_page(base + done)); }
            return -1;
        }
    }
    heap_mapped += bytes;
    return 0;
}

static void memory_init(uint32_t magic, struct mb_info *mbi) {
    pmm_init();
    if (magic == 0x2BADB002 && (mbi->flags & 64)) {
        uint32_t p = mbi->mmap_addr, end = p + mbi->mmap_length;
        while (p < end) {
            struct mb_mmap *m = (struct mb_mmap *)p;
            if (m->type == 1 && m->addr < 0x100000000ull) {
                uint64_t len = m->len;
                if (m->addr + len > 0x100000000ull) len = 0x100000000ull - m->addr;
                pmm_add_free_region((uint32_t)m->addr, (uint32_t)len);
            }
            p += m->size + 4;
        }
    } else if (magic == 0x2BADB002 && (mbi->flags & 1)) {   /* fall back to basic memory info */
        pmm_add_free_region(0, mbi->mem_lower * 1024);
        pmm_add_free_region(0x100000, mbi->mem_upper * 1024);
    }
    pmm_reserve_region(0, 0x100000);                        /* BIOS data, VGA, real-mode area */
    pmm_reserve_region((uint32_t)&kernel_start, (uint32_t)(&kernel_end - &kernel_start));
    pmm_reserve_region((uint32_t)mbi, sizeof *mbi);
    if (mbi->flags & 64) pmm_reserve_region(mbi->mmap_addr, mbi->mmap_length);
    uint32_t limit = pmm_max_usable_addr();
    if (limit > PMM_MAX_PHYS) limit = PMM_MAX_PHYS;
    paging_init(limit);
    kheap_init((void *)HEAP_VIRT, 256 * 1024, HEAP_MAX, heap_grow);
}

/* ---------- shell ---------- */
#define MAX_HANDLES 16
static void *handles[MAX_HANDLES]; static uint32_t hsize[MAX_HANDLES];
static uint32_t frames[16]; static int nframes;

static int getch(void) {
    for (;;) {
        if (inb(COM1 + 5) & 1) return inb(COM1);
        if (khead != ktail) { char c = kbuf[ktail]; ktail = (ktail + 1) % 64; return c; }
        __asm__ volatile("hlt");
    }
}
static int readline(char *buf, int max) {
    int n = 0;
    for (;;) {
        int c = getch();
        if (c == '\r' || c == '\n') { putc('\n'); buf[n] = 0; return n; }
        if ((c == '\b' || c == 127)) { if (n) { n--; putc('\b'); } }
        else if (c >= 32 && c < 127 && n < max - 1) { buf[n++] = (char)c; putc((char)c); }
    }
}

static void cmd_meminfo(void) {
    puts("Physical: "); put_dec(pmm_free_frames()); puts(" free / ");
    put_dec(pmm_total_frames()); puts(" usable frames (");
    put_dec(pmm_free_frames() * 4 / 1024); puts(" MB free)\n");
    struct kheap_stats s; kheap_get_stats(&s);
    puts("Heap: "); put_dec(s.total); puts(" bytes mapped, "); put_dec(s.used_bytes);
    puts(" used, "); put_dec(s.free_bytes); puts(" free, largest free ");
    put_dec(s.largest_free); puts(", "); put_dec(s.blocks); puts(" blocks, ");
    put_dec(paging_tables_used()); puts(" page tables\n");
}

static void run(char *line) {
    char *argv[4]; int argc = 0;
    for (char *p = line; *p && argc < 4; ) {
        while (*p == ' ') *p++ = 0;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
    }
    if (!argc) return;
    const char *c = argv[0]; uint32_t v;
    if (streq(c, "help")) {
        puts("help meminfo uptime selftest\n"
             "frame | unframe        allocate / free a physical frame\n"
             "malloc N | free ID     kmalloc N bytes / kfree handle ID\n"
             "heapcheck              verify heap block chain\n"
             "translate ADDR         virtual -> physical (decimal or 0x hex)\n"
             "crash                  touch an unmapped page (expect PAGE FAULT)\n"
             "halt                   stop the machine\n");
    } else if (streq(c, "meminfo")) cmd_meminfo();
    else if (streq(c, "uptime")) { puts("ticks: "); put_dec(timer_ticks()); puts(" (100 Hz)\n"); }
    else if (streq(c, "selftest")) run_selftest();
    else if (streq(c, "frame")) {
        if (nframes >= 16) { puts("frame list full\n"); return; }
        uint32_t f = pmm_alloc_frame();
        if (!f) { puts("out of physical memory\n"); return; }
        frames[nframes++] = f; puts("frame "); put_hex(f); puts("\n");
    } else if (streq(c, "unframe")) {
        if (!nframes) { puts("no frames held\n"); return; }
        uint32_t f = frames[--nframes]; pmm_free_frame(f); puts("freed "); put_hex(f); puts("\n");
    } else if (streq(c, "malloc") && argc > 1 && parse_num(argv[1], &v) == 0) {
        for (int i = 0; i < MAX_HANDLES; i++) if (!handles[i]) {
            void *p = kmalloc(v);
            if (!p) { puts("kmalloc failed\n"); return; }
            handles[i] = p; hsize[i] = v;
            puts("id "); put_dec(i); puts(" = "); put_hex((uint32_t)p); puts("\n"); return;
        }
        puts("handle table full\n");
    } else if (streq(c, "free") && argc > 1 && parse_num(argv[1], &v) == 0 && v < MAX_HANDLES && handles[v]) {
        kfree(handles[v]); handles[v] = 0; puts("freed\n");
    } else if (streq(c, "heapcheck")) puts(kheap_check() == 0 ? "heap OK\n" : "heap CORRUPT\n");
    else if (streq(c, "translate") && argc > 1 && parse_num(argv[1], &v) == 0) {
        uint32_t ph;
        if (paging_translate(v, &ph) == 0) { put_hex(v); puts(" -> "); put_hex(ph); puts("\n"); }
        else { put_hex(v); puts(" not mapped\n"); }
    } else if (streq(c, "crash")) { *(volatile uint32_t *)0xE0000000u = 1; }
    else if (streq(c, "halt")) { puts("halting\n"); qemu_exit(0x10); for (;;) __asm__ volatile("cli; hlt"); }
    else puts("unknown or malformed command (try: help)\n");
}

void kernel_main(uint32_t magic, struct mb_info *mbi) {
    serial_init(); screen_clear();
    puts("StrataOS booting...\n");
    if (magic != 0x2BADB002) { puts("Not booted by a Multiboot loader!\n"); for (;;) __asm__ volatile("hlt"); }
    gdt_init();   puts("[ok] GDT\n");
    pic_init(); idt_init(); puts("[ok] IDT + PIC\n");
    timer_init(); puts("[ok] PIT timer (100 Hz)\n");
    memory_init(magic, mbi);
    puts("[ok] Physical memory manager: "); put_dec(pmm_free_frames()); puts(" free frames\n");
    puts("[ok] Paging enabled, identity map to "); put_dec(pmm_max_usable_addr() / (1024 * 1024)); puts(" MB\n");
    puts("[ok] Kernel heap at "); put_hex(HEAP_VIRT); puts("\n");
    __asm__ volatile("sti");
    puts("Type 'help' for commands.\n\n");
    char line[64];
    for (;;) { puts("StrataOS> "); readline(line, sizeof line); run(line); }
}
