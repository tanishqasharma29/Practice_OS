#ifndef ATA_H
#define ATA_H
#include <stdint.h>

#define SECTOR_SIZE 512

/* Error codes (negative returns) */
#define ATA_OK           0
#define ATA_ENODEV     (-1)   /* no drive, or not an ATA disk */
#define ATA_ETIMEOUT   (-2)
#define ATA_EIO        (-3)   /* drive reported an error or fault */
#define ATA_ERANGE     (-4)   /* LBA outside the disk */

/* Probe the primary-bus master with IDENTIFY. Returns 0 if a LBA28 disk was found. */
int      ata_init(void);
int      ata_present(void);
uint32_t ata_sector_count(void);     /* 0 if no disk */
const char *ata_model(void);         /* NUL-terminated model string, "" if no disk */

/* One 512-byte sector, LBA28. Polled PIO, bounded waits, errors returned (never hangs). */
int ata_read_sector(uint32_t lba, void *buffer);
int ata_write_sector(uint32_t lba, const void *buffer);   /* flushes the drive cache */
#endif
