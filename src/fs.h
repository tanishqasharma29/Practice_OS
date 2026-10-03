#ifndef FS_H
#define FS_H
#include <stddef.h>
#include <stdint.h>

/* StrataFS: a small on-disk filesystem with directories. Pure logic over a sector device, so the host
   tests run the same code on a RAM image. Layout: LBA 0 superblock, LBA 1-8 directory table
   (64 entries of 64 bytes), data from LBA 9. Each file is one contiguous extent. */

#define FS_NAME_MAX   31
#define FS_MAX_ENTRIES 64
#define FS_MAX_FILE   (64u * 1024)     /* bytes per file */

#define FS_OK         0
#define FS_EIO       (-1)   /* the block device failed */
#define FS_ENOFS     (-2)   /* disk has no StrataFS (run format) */
#define FS_ECORRUPT  (-3)   /* StrataFS metadata is inconsistent; nothing was changed */
#define FS_ENOENT    (-4)
#define FS_EEXIST    (-5)
#define FS_ENOTDIR   (-6)
#define FS_EISDIR    (-7)
#define FS_ENOSPC    (-8)   /* no free directory entry or no contiguous space */
#define FS_EINVAL    (-9)   /* bad name or path */
#define FS_ENOTEMPTY (-10)
#define FS_EBUSY     (-11)  /* would remove the current directory or one of its parents */
#define FS_ETOOBIG   (-12)
#define FS_ENOTMOUNTED (-13)

struct fs_dev {
    int (*read)(uint32_t lba, void *buf);
    int (*write)(uint32_t lba, const void *buf);
    uint32_t sectors;
};

struct fs_stat { uint8_t is_dir; uint32_t size; };
typedef void (*fs_list_cb)(void *ctx, const char *name, int is_dir, uint32_t size);

int  fs_mount(const struct fs_dev *dev);      /* FS_OK, FS_ENOFS, FS_ECORRUPT or FS_EIO; never writes */
int  fs_format(const struct fs_dev *dev);     /* ERASES the directory table of this device, then mounts it */
int  fs_is_mounted(void);
const char *fs_strerror(int err);

int  fs_list(const char *path, fs_list_cb cb, void *ctx);          /* path may be NULL for cwd */
int  fs_stat(const char *path, struct fs_stat *st);
int  fs_read(const char *path, void *buf, uint32_t max, uint32_t *size);   /* FS_ETOOBIG if buf too small */
int  fs_write(const char *path, const void *data, uint32_t len);   /* create or replace a file */
int  fs_mkdir(const char *path);
int  fs_remove(const char *path);                                  /* a file, or an empty directory */
int  fs_chdir(const char *path);
int  fs_getcwd(char *buf, uint32_t max);
void fs_usage(uint32_t *files, uint32_t *dirs, uint32_t *free_sectors, uint32_t *total_data_sectors);
#endif
