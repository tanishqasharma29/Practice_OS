/* ATA PIO driver for the primary IDE bus, master drive, LBA28, polling only (IRQ 14 stays masked). */
#include "ata.h"
#include "kernel.h"

#define DATA     0x1F0
#define ERR      0x1F1
#define SECCNT   0x1F2
#define LBA_LO   0x1F3
#define LBA_MID  0x1F4
#define LBA_HI   0x1F5
#define DRVHEAD  0x1F6
#define STATUS   0x1F7
#define CMD      0x1F7
#define DEVCTL   0x3F6

#define ST_ERR 0x01
#define ST_DRQ 0x08
#define ST_DF  0x20
#define ST_BSY 0x80

#define CMD_READ     0x20
#define CMD_WRITE    0x30
#define CMD_FLUSH    0xE7
#define CMD_IDENTIFY 0xEC

#define POLL_LIMIT 2000000   /* port reads; each takes ~1 us or more, so this is seconds, not forever */

static int present;
static uint32_t sectors;
static char model[41];

static inline void insw(uint16_t port, void *addr, uint32_t count) {
    __asm__ volatile("rep insw" : "+D"(addr), "+c"(count) : "d"(port) : "memory");
}
static inline void outsw(uint16_t port, const void *addr, uint32_t count) {
    __asm__ volatile("rep outsw" : "+S"(addr), "+c"(count) : "d"(port) : "memory");
}

static void delay400ns(void) { for (int i = 0; i < 4; i++) inb(DEVCTL); }

/* Wait until BSY clears. Returns the final status byte, or -1 on timeout. */
static int wait_not_busy(void) {
    for (int i = 0; i < POLL_LIMIT; i++) {
        uint8_t s = inb(STATUS);
        if (!(s & ST_BSY)) return s;
    }
    return -1;
}
/* Wait for DRQ (data ready). 0 ok, else an ATA_E* code. */
static int wait_drq(void) {
    delay400ns();
    for (int i = 0; i < POLL_LIMIT; i++) {
        uint8_t s = inb(STATUS);
        if (s & ST_BSY) continue;
        if (s & (ST_ERR | ST_DF)) return ATA_EIO;
        if (s & ST_DRQ) return ATA_OK;
    }
    return ATA_ETIMEOUT;
}

static void select_lba(uint32_t lba) {
    outb(DRVHEAD, 0xE0 | ((lba >> 24) & 0x0F));            /* master, LBA mode */
    outb(SECCNT, 1);
    outb(LBA_LO, (uint8_t)lba);
    outb(LBA_MID, (uint8_t)(lba >> 8));
    outb(LBA_HI, (uint8_t)(lba >> 16));
}

int ata_init(void) {
    present = 0; sectors = 0; model[0] = 0;
    outb(DEVCTL, 0x02);                                    /* nIEN: no IRQ 14 from the drive */
    outb(DRVHEAD, 0xA0);
    delay400ns();
    if (inb(STATUS) == 0xFF) return ATA_ENODEV;            /* floating bus: nothing attached */
    outb(SECCNT, 0); outb(LBA_LO, 0); outb(LBA_MID, 0); outb(LBA_HI, 0);
    outb(CMD, CMD_IDENTIFY);
    if (inb(STATUS) == 0) return ATA_ENODEV;
    int s = wait_not_busy();
    if (s < 0) return ATA_ETIMEOUT;
    if (inb(LBA_MID) || inb(LBA_HI)) return ATA_ENODEV;    /* ATAPI / SATA signature, not plain ATA */
    int r = wait_drq();
    if (r) return r;
    uint16_t id[256];
    insw(DATA, id, 256);
    if (!(id[49] & (1 << 9))) return ATA_ENODEV;           /* no LBA support */
    sectors = (uint32_t)id[60] | ((uint32_t)id[61] << 16);
    if (sectors == 0) return ATA_ENODEV;
    if (sectors > 0x0FFFFFFFu) sectors = 0x0FFFFFFFu;      /* LBA28 limit */
    for (int i = 0; i < 20; i++) { model[2 * i] = (char)(id[27 + i] >> 8); model[2 * i + 1] = (char)id[27 + i]; }
    model[40] = 0;
    for (int i = 39; i >= 0 && (model[i] == ' ' || model[i] == 0); i--) model[i] = 0;
    present = 1;
    return ATA_OK;
}

int ata_present(void) { return present; }
uint32_t ata_sector_count(void) { return present ? sectors : 0; }
const char *ata_model(void) { return model; }

int ata_read_sector(uint32_t lba, void *buffer) {
    if (!present) return ATA_ENODEV;
    if (lba >= sectors) return ATA_ERANGE;
    uint32_t f = irq_save();
    int r = 0, s = wait_not_busy();
    if (s < 0) r = ATA_ETIMEOUT;
    else {
        select_lba(lba);
        outb(CMD, CMD_READ);
        r = wait_drq();
        if (r == 0) insw(DATA, buffer, SECTOR_SIZE / 2);
    }
    irq_restore(f);
    return r;
}

int ata_write_sector(uint32_t lba, const void *buffer) {
    if (!present) return ATA_ENODEV;
    if (lba >= sectors) return ATA_ERANGE;
    uint32_t f = irq_save();
    int r = 0, s = wait_not_busy();
    if (s < 0) r = ATA_ETIMEOUT;
    else {
        select_lba(lba);
        outb(CMD, CMD_WRITE);
        r = wait_drq();
        if (r == 0) {
            outsw(DATA, buffer, SECTOR_SIZE / 2);
            outb(CMD, CMD_FLUSH);
            s = wait_not_busy();
            if (s < 0) r = ATA_ETIMEOUT;
            else if (s & (ST_ERR | ST_DF)) r = ATA_EIO;
        }
    }
    irq_restore(f);
    return r;
}
