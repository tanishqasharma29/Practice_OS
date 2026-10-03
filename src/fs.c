#include "fs.h"

#define SECT 512u
#define SB_LBA 0u
#define DIR_LBA 1u
#define DIR_SECTORS 8u
#define DATA_LBA (DIR_LBA + DIR_SECTORS)
#define ROOT 0xFFu
#define T_FREE 0
#define T_FILE 1
#define T_DIR 2
#define MAGIC "STRATAFS"
#define VERSION 1u

struct entry {                       /* 64 bytes */
    char name[32];
    uint8_t type, parent, pad[2];
    uint32_t size, start, nsect;
    uint8_t reserved[16];
};
_Static_assert(sizeof(struct entry) == 64, "entry size");
_Static_assert(FS_MAX_ENTRIES * sizeof(struct entry) == DIR_SECTORS * SECT, "dir table size");

struct superblock { char magic[8]; uint32_t version, total_sectors, dir_lba, dir_sectors, data_lba; };

static struct fs_dev dev;
static int mounted;
static struct entry dir[FS_MAX_ENTRIES];
static uint8_t cwd = ROOT;

static int streq_(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void zero(void *p, size_t n) { uint8_t *b = p; while (n--) *b++ = 0; }
static void copy(void *d, const void *s, size_t n) { uint8_t *a = d; const uint8_t *b = s; while (n--) *a++ = *b++; }
static size_t strlen_(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
static uint32_t sectors_for(uint32_t bytes) { return (bytes + SECT - 1) / SECT; }

const char *fs_strerror(int e) {
    switch (e) {
    case FS_OK: return "ok"; case FS_EIO: return "disk I/O error"; case FS_ENOFS: return "no StrataFS on disk (use: format yes)";
    case FS_ECORRUPT: return "filesystem metadata is inconsistent"; case FS_ENOENT: return "no such file or directory";
    case FS_EEXIST: return "already exists"; case FS_ENOTDIR: return "not a directory"; case FS_EISDIR: return "is a directory";
    case FS_ENOSPC: return "no space left"; case FS_EINVAL: return "invalid name or path";
    case FS_ENOTEMPTY: return "directory not empty"; case FS_EBUSY: return "cannot remove the current directory or a parent of it";
    case FS_ETOOBIG: return "file too large"; case FS_ENOTMOUNTED: return "no filesystem mounted";
    }
    return "error";
}

static int valid_name(const char *n) {
    int len = 0;
    for (; n[len]; len++) if ((unsigned char)n[len] <= 32 || (unsigned char)n[len] >= 127 || n[len] == '/') return 0;
    if (len == 0 || len > FS_NAME_MAX) return 0;
    if (streq_(n, ".") || streq_(n, "..")) return 0;
    return 1;
}

static int write_dir_sector(unsigned s) { return dev.write(DIR_LBA + s, (uint8_t *)dir + s * SECT) ? FS_EIO : FS_OK; }

/* Validate everything read from disk before trusting it. */
static int validate(void) {
    for (unsigned i = 0; i < FS_MAX_ENTRIES; i++) {
        struct entry *e = &dir[i];
        if (e->type == T_FREE) continue;
        if (e->type != T_FILE && e->type != T_DIR) return FS_ECORRUPT;
        int ok = 0;
        for (int k = 0; k < 32; k++) if (e->name[k] == 0) { ok = 1; break; }
        if (!ok || !valid_name(e->name)) return FS_ECORRUPT;
        if (e->parent != ROOT && (e->parent >= FS_MAX_ENTRIES || dir[e->parent].type != T_DIR || e->parent == i)) return FS_ECORRUPT;
        if (e->type == T_DIR) { if (e->size || e->nsect) return FS_ECORRUPT; }
        else {
            if (e->size > FS_MAX_FILE || e->nsect != sectors_for(e->size)) return FS_ECORRUPT;
            if (e->nsect && (e->start < DATA_LBA || e->start > dev.sectors || e->nsect > dev.sectors - e->start)) return FS_ECORRUPT;
        }
        for (unsigned j = 0; j < i; j++) {                    /* duplicate name, overlapping extents */
            struct entry *o = &dir[j];
            if (o->type == T_FREE) continue;
            if (o->parent == e->parent && streq_(o->name, e->name)) return FS_ECORRUPT;
            if (e->type == T_FILE && o->type == T_FILE && e->nsect && o->nsect &&
                e->start < o->start + o->nsect && o->start < e->start + e->nsect) return FS_ECORRUPT;
        }
        unsigned depth = 0;                                    /* parent chains must reach the root */
        for (uint8_t p = e->parent; p != ROOT; p = dir[p].parent) if (++depth > FS_MAX_ENTRIES) return FS_ECORRUPT;
    }
    return FS_OK;
}

int fs_mount(const struct fs_dev *d) {
    uint8_t sec[SECT];
    mounted = 0; cwd = ROOT;
    if (!d || !d->read || !d->write || d->sectors <= DATA_LBA) return FS_EINVAL;
    dev = *d;
    if (dev.read(SB_LBA, sec)) return FS_EIO;
    struct superblock sb; copy(&sb, sec, sizeof sb);
    for (int i = 0; i < 8; i++) if (sb.magic[i] != MAGIC[i]) return FS_ENOFS;
    if (sb.version != VERSION || sb.dir_lba != DIR_LBA || sb.dir_sectors != DIR_SECTORS || sb.data_lba != DATA_LBA ||
        sb.total_sectors > dev.sectors) return FS_ECORRUPT;
    for (unsigned s = 0; s < DIR_SECTORS; s++) if (dev.read(DIR_LBA + s, (uint8_t *)dir + s * SECT)) return FS_EIO;
    int r = validate();
    if (r) { zero(dir, sizeof dir); return r; }
    mounted = 1;
    return FS_OK;
}

int fs_format(const struct fs_dev *d) {
    if (!d || !d->read || !d->write || d->sectors <= DATA_LBA) return FS_EINVAL;
    dev = *d; mounted = 0; cwd = ROOT;
    zero(dir, sizeof dir);
    for (unsigned s = 0; s < DIR_SECTORS; s++) if (write_dir_sector(s)) return FS_EIO;
    uint8_t sec[SECT]; zero(sec, sizeof sec);
    struct superblock sb = { {0}, VERSION, dev.sectors, DIR_LBA, DIR_SECTORS, DATA_LBA };
    copy(sb.magic, MAGIC, 8); copy(sec, &sb, sizeof sb);
    if (dev.write(SB_LBA, sec)) return FS_EIO;                /* superblock last: a half-format stays "no filesystem" */
    mounted = 1;
    return FS_OK;
}

int fs_is_mounted(void) { return mounted; }

/* ---------- path handling ---------- */
static int find_child(uint8_t parent, const char *name) {
    for (int i = 0; i < FS_MAX_ENTRIES; i++)
        if (dir[i].type != T_FREE && dir[i].parent == parent && streq_(dir[i].name, name)) return i;
    return -1;
}

/* Walk a path. If want_leaf is set, stop before the last component and copy it to leaf (the parent must exist).
   Returns FS_OK and sets *node (ROOT for "/"), or an error. */
static int walk(const char *path, int want_leaf, uint8_t *node, char *leaf) {
    uint8_t cur = path && path[0] == '/' ? ROOT : cwd;
    const char *p = path ? path : "";
    if (want_leaf) *leaf = 0;
    for (;;) {
        while (*p == '/') p++;
        if (!*p) break;
        char comp[FS_NAME_MAX + 2]; int n = 0;
        while (*p && *p != '/') { if (n >= FS_NAME_MAX) return FS_EINVAL; comp[n++] = *p++; }
        comp[n] = 0;
        const char *q = p; while (*q == '/') q++;
        int last = (*q == 0);
        if (want_leaf && last && !streq_(comp, ".") && !streq_(comp, "..")) { copy(leaf, comp, (size_t)n + 1); break; }
        if (streq_(comp, ".")) continue;
        if (streq_(comp, "..")) { if (cur != ROOT) cur = dir[cur].parent; continue; }
        int i = find_child(cur, comp);
        if (i < 0) return FS_ENOENT;
        if (dir[i].type != T_DIR) return FS_ENOTDIR;
        cur = (uint8_t)i;
    }
    *node = cur;
    return FS_OK;
}

/* Resolve to an existing entry; *idx == ROOT means the root directory. */
static int lookup(const char *path, uint8_t *idx) {
    uint8_t parent; char leaf[FS_NAME_MAX + 1];
    int r = walk(path, 1, &parent, leaf);
    if (r) return r;
    if (!leaf[0]) { *idx = parent; return FS_OK; }       /* path ended in ".", ".." or "/" */
    int i = find_child(parent, leaf);
    if (i < 0) return FS_ENOENT;
    *idx = (uint8_t)i; return FS_OK;
}

static int is_ancestor_or_self(uint8_t a, uint8_t node) {
    for (uint8_t p = node;; p = dir[p].parent) { if (p == a) return 1; if (p == ROOT) return 0; }
}

/* ---------- allocation ---------- */
static int overlaps(uint32_t start, uint32_t n) {
    for (int i = 0; i < FS_MAX_ENTRIES; i++)
        if (dir[i].type == T_FILE && dir[i].nsect && start < dir[i].start + dir[i].nsect && dir[i].start < start + n) return 1;
    return 0;
}
static int alloc_extent(uint32_t n, uint32_t *start) {
    if (n == 0) { *start = 0; return 0; }
    uint32_t cand = DATA_LBA;
    for (int round = 0; round <= FS_MAX_ENTRIES; round++) {
        if (cand + n <= dev.sectors && !overlaps(cand, n)) { *start = cand; return 0; }
        uint32_t next = 0xFFFFFFFFu;                          /* try the end of the nearest extent past cand */
        for (int i = 0; i < FS_MAX_ENTRIES; i++)
            if (dir[i].type == T_FILE && dir[i].nsect) { uint32_t e = dir[i].start + dir[i].nsect; if (e > cand && e < next) next = e; }
        if (next == 0xFFFFFFFFu) break;
        cand = next;
    }
    return -1;
}

/* ---------- operations ---------- */
int fs_list(const char *path, fs_list_cb cb, void *ctx) {
    if (!mounted) return FS_ENOTMOUNTED;
    uint8_t n; int r = lookup(path, &n);
    if (r) return r;
    if (n != ROOT && dir[n].type != T_DIR) { cb(ctx, dir[n].name, 0, dir[n].size); return FS_OK; }
    for (int i = 0; i < FS_MAX_ENTRIES; i++)
        if (dir[i].type != T_FREE && dir[i].parent == n) cb(ctx, dir[i].name, dir[i].type == T_DIR, dir[i].size);
    return FS_OK;
}

int fs_stat(const char *path, struct fs_stat *st) {
    if (!mounted) return FS_ENOTMOUNTED;
    uint8_t n; int r = lookup(path, &n);
    if (r) return r;
    st->is_dir = n == ROOT || dir[n].type == T_DIR; st->size = n == ROOT ? 0 : dir[n].size;
    return FS_OK;
}

int fs_read(const char *path, void *buf, uint32_t max, uint32_t *size) {
    if (!mounted) return FS_ENOTMOUNTED;
    uint8_t n; int r = lookup(path, &n);
    if (r) return r;
    if (n == ROOT || dir[n].type != T_FILE) return FS_EISDIR;
    if (dir[n].size > max) return FS_ETOOBIG;
    uint8_t sec[SECT], *out = buf;
    for (uint32_t s = 0, left = dir[n].size; s < dir[n].nsect; s++) {
        if (dev.read(dir[n].start + s, sec)) return FS_EIO;
        uint32_t c = left < SECT ? left : SECT;
        copy(out, sec, c); out += c; left -= c;
    }
    *size = dir[n].size;
    return FS_OK;
}

static int free_slot(void) { for (int i = 0; i < FS_MAX_ENTRIES; i++) if (dir[i].type == T_FREE) return i; return -1; }

/* Store a modified entry; restore the in-memory copy if the disk write fails. */
static int commit(int i, const struct entry *old) {
    int r = write_dir_sector((unsigned)i / 8);
    if (r) dir[i] = *old;
    return r;
}

int fs_write(const char *path, const void *data, uint32_t len) {
    if (!mounted) return FS_ENOTMOUNTED;
    if (len > FS_MAX_FILE) return FS_ETOOBIG;
    uint8_t parent; char leaf[FS_NAME_MAX + 1];
    int r = walk(path, 1, &parent, leaf);
    if (r) return r;
    if (!leaf[0] || !valid_name(leaf)) return FS_EINVAL;
    int i = find_child(parent, leaf);
    if (i >= 0 && dir[i].type != T_FILE) return FS_EISDIR;
    int created = 0;
    if (i < 0) { i = free_slot(); if (i < 0) return FS_ENOSPC; created = 1; }
    uint32_t n = sectors_for(len), start;
    /* New data goes to space not used by any file, including the old copy of this one. */
    if (alloc_extent(n, &start)) return FS_ENOSPC;
    const uint8_t *src = data; uint8_t sec[SECT];
    for (uint32_t s = 0, left = len; s < n; s++) {
        uint32_t c = left < SECT ? left : SECT;
        zero(sec, sizeof sec); copy(sec, src, c); src += c; left -= c;
        if (dev.write(start + s, sec)) return FS_EIO;          /* directory untouched: old content still intact */
    }
    struct entry old = dir[i];
    if (created) { zero(&dir[i], sizeof dir[i]); copy(dir[i].name, leaf, strlen_(leaf) + 1); dir[i].type = T_FILE; dir[i].parent = parent; }
    dir[i].size = len; dir[i].start = start; dir[i].nsect = n;
    return commit(i, &old);
}

int fs_mkdir(const char *path) {
    if (!mounted) return FS_ENOTMOUNTED;
    uint8_t parent; char leaf[FS_NAME_MAX + 1];
    int r = walk(path, 1, &parent, leaf);
    if (r) return r;
    if (!leaf[0] || !valid_name(leaf)) return FS_EINVAL;
    if (find_child(parent, leaf) >= 0) return FS_EEXIST;
    int i = free_slot();
    if (i < 0) return FS_ENOSPC;
    struct entry old = dir[i];
    zero(&dir[i], sizeof dir[i]); copy(dir[i].name, leaf, strlen_(leaf) + 1); dir[i].type = T_DIR; dir[i].parent = parent;
    return commit(i, &old);
}

int fs_remove(const char *path) {
    if (!mounted) return FS_ENOTMOUNTED;
    uint8_t n; int r = lookup(path, &n);
    if (r) return r;
    if (n == ROOT) return FS_EBUSY;
    if (dir[n].type == T_DIR) {
        for (int i = 0; i < FS_MAX_ENTRIES; i++) if (dir[i].type != T_FREE && dir[i].parent == n) return FS_ENOTEMPTY;
        if (is_ancestor_or_self(n, cwd)) return FS_EBUSY;
    }
    struct entry old = dir[n];
    zero(&dir[n], sizeof dir[n]);
    return commit(n, &old);
}

int fs_chdir(const char *path) {
    if (!mounted) return FS_ENOTMOUNTED;
    uint8_t n; int r = lookup(path, &n);
    if (r) return r;
    if (n != ROOT && dir[n].type != T_DIR) return FS_ENOTDIR;
    cwd = n; return FS_OK;
}

int fs_getcwd(char *buf, uint32_t max) {
    if (!mounted) return FS_ENOTMOUNTED;
    char tmp[FS_MAX_ENTRIES * (FS_NAME_MAX + 1) + 2]; uint32_t pos = sizeof tmp - 1;
    tmp[pos] = 0;
    for (uint8_t p = cwd; p != ROOT; p = dir[p].parent) {
        uint32_t l = (uint32_t)strlen_(dir[p].name);
        pos -= l; copy(tmp + pos, dir[p].name, l); tmp[--pos] = '/';
    }
    if (tmp[pos] == 0) tmp[--pos] = '/';
    if (strlen_(tmp + pos) + 1 > max) return FS_ETOOBIG;
    copy(buf, tmp + pos, strlen_(tmp + pos) + 1);
    return FS_OK;
}

void fs_usage(uint32_t *files, uint32_t *dirs, uint32_t *free_sectors, uint32_t *total) {
    uint32_t f = 0, d = 0, used = 0;
    for (int i = 0; i < FS_MAX_ENTRIES; i++) {
        if (dir[i].type == T_FILE) { f++; used += dir[i].nsect; } else if (dir[i].type == T_DIR) d++;
    }
    *files = f; *dirs = d; *total = mounted ? dev.sectors - DATA_LBA : 0; *free_sectors = *total - used;
}
