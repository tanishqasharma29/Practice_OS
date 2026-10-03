#include "kheap.h"

#define MAGIC_USED 0xA110C8EDu
#define MAGIC_FREE 0xF4EEB10Cu
#define ALIGN      16u
#define PAGE       4096u
#define MIN_SPLIT  32u

typedef struct block {
    uint32_t magic;
    uint32_t is_free;
    size_t   size;            /* payload bytes */
    struct block *next, *prev;
} __attribute__((aligned(16))) block_t;

#define HDR sizeof(block_t)

static block_t *head, *tail;
static uint8_t *heap_base;
static size_t   heap_size, heap_max;
static kheap_grow_fn grow_fn;
static kheap_lock_fn lock_fn;
static kheap_unlock_fn unlock_fn;
static uint32_t lock(void) { return lock_fn ? lock_fn() : 0; }
static void unlock(uint32_t s) { if (unlock_fn) unlock_fn(s); }
void kheap_set_lock(kheap_lock_fn l, kheap_unlock_fn u) { lock_fn = l; unlock_fn = u; }

static size_t round_up(size_t v, size_t a) { return (v + a - 1) & ~(a - 1); }

static int extend(size_t need_payload) {
    size_t bytes = round_up(need_payload + HDR, PAGE);
    if (bytes < 16 * PAGE) bytes = 16 * PAGE;            /* grow in >= 64 KiB steps */
    if (heap_size + bytes > heap_max) {
        bytes = round_up(need_payload + HDR, PAGE);
        if (heap_size + bytes > heap_max) return -1;
    }
    if (grow_fn(heap_base + heap_size, bytes) != 0) return -1;
    if (tail && tail->is_free) {                          /* merge into trailing free block */
        tail->size += bytes;
    } else {
        block_t *b = (block_t *)(heap_base + heap_size);
        b->magic = MAGIC_FREE; b->is_free = 1; b->size = bytes - HDR;
        b->next = NULL; b->prev = tail;
        if (tail) tail->next = b; else head = b;
        tail = b;
    }
    heap_size += bytes;
    return 0;
}

void kheap_init(void *base, size_t initial, size_t max, kheap_grow_fn grow) {
    heap_base = base; heap_size = 0; heap_max = max; grow_fn = grow;
    head = tail = NULL;
    extend(initial > HDR ? initial - HDR : 0);
}

static void *take(block_t *b, size_t size) {
    if (b->size >= size + HDR + MIN_SPLIT) {              /* split */
        block_t *n = (block_t *)((uint8_t *)b + HDR + size);
        n->magic = MAGIC_FREE; n->is_free = 1; n->size = b->size - size - HDR;
        n->prev = b; n->next = b->next;
        if (b->next) b->next->prev = n; else tail = n;
        b->next = n; b->size = size;
    }
    b->is_free = 0; b->magic = MAGIC_USED;
    return (uint8_t *)b + HDR;
}

static void *kmalloc_locked(size_t size) {
    if (size == 0 || size > heap_max) return NULL;
    size = round_up(size, ALIGN);
    for (int attempt = 0; attempt < 2; attempt++) {
        for (block_t *b = head; b; b = b->next)
            if (b->is_free && b->size >= size) return take(b, size);
        if (attempt == 0 && extend(size) != 0) return NULL;
    }
    return NULL;
}

static int kfree_locked(void *ptr) {
    if (!ptr) return 0;
    uint8_t *p = ptr;
    if (p < heap_base + HDR || p >= heap_base + heap_size || ((uintptr_t)p % ALIGN)) return -1;
    block_t *b = (block_t *)(p - HDR);
    if (b->magic != MAGIC_USED || b->is_free) return -1;      /* bad pointer or double free */
    b->is_free = 1; b->magic = MAGIC_FREE;
    if (b->next && b->next->is_free) {                        /* merge with next */
        block_t *n = b->next;
        b->size += HDR + n->size; b->next = n->next;
        if (n->next) n->next->prev = b; else tail = b;
        n->magic = 0;
    }
    if (b->prev && b->prev->is_free) {                        /* merge with previous */
        block_t *pr = b->prev;
        pr->size += HDR + b->size; pr->next = b->next;
        if (b->next) b->next->prev = pr; else tail = pr;
        b->magic = 0;
    }
    return 0;
}

static int kheap_check_locked(void) {
    size_t total = 0; block_t *prev = NULL;
    for (block_t *b = head; b; prev = b, b = b->next) {
        if ((uint8_t *)b != heap_base + total) return -1;     /* contiguity */
        if (b->prev != prev) return -1;
        if (b->magic != (b->is_free ? MAGIC_FREE : MAGIC_USED)) return -1;
        if (b->size % ALIGN) return -1;
        if (prev && prev->is_free && b->is_free) return -1;   /* uncoalesced neighbours */
        total += HDR + b->size;
    }
    if (prev != tail || total != heap_size) return -1;
    return 0;
}

static void stats_locked(struct kheap_stats *s) {
    s->total = heap_size; s->used_bytes = s->free_bytes = s->largest_free = 0;
    s->blocks = s->free_blocks = 0;
    for (block_t *b = head; b; b = b->next) {
        s->blocks++;
        if (b->is_free) {
            s->free_blocks++; s->free_bytes += b->size;
            if (b->size > s->largest_free) s->largest_free = b->size;
        } else s->used_bytes += b->size;
    }
}


void *kmalloc(size_t size) { uint32_t f = lock(); void *r = kmalloc_locked(size); unlock(f); return r; }
int kfree(void *ptr) { uint32_t f = lock(); int r = kfree_locked(ptr); unlock(f); return r; }
int kheap_check(void) { uint32_t f = lock(); int r = kheap_check_locked(); unlock(f); return r; }
void kheap_get_stats(struct kheap_stats *s) { uint32_t f = lock(); stats_locked(s); unlock(f); }
