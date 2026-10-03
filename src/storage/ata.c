
#include "ata.h"

#define ATA_IO 0x1F0
#define ATA_CTRL 0x3F6
#define ATA_DATA 0
#define ATA_SECCOUNT 2
#define ATA_LBA0 3
#define ATA_LBA1 4
#define ATA_LBA2 5
#define ATA_DEVICE 6
#define ATA_STATUS 7
#define ATA_COMMAND 7

#define ATA_SR_ERR 0x01
#define ATA_SR_DRQ 0x08
#define ATA_SR_DF 0x20
#define ATA_SR_BSY 0x80

#define ATA_CMD_IDENTIFY 0xEC
#define ATA_CMD_READ 0x20
#define ATA_CMD_WRITE 0x30
#define ATA_CMD_FLUSH 0xE7

#define ATA_SIGNATURE 0x4D594F53u
#define ATA_SECTOR_SIZE 512

#ifdef UNIT_TEST
static uint32_t ata_ports[65536];
static inline void out8(uint16_t p, uint8_t v) { ata_ports[p] = (ata_ports[p] & 0xFFFFFF00u) | v; }
static inline uint8_t in8(uint16_t p) { return (uint8_t)ata_ports[p]; }
static inline void out16(uint16_t p, uint16_t v) { ata_ports[p] = (ata_ports[p] & 0xFFFF0000u) | v; }
static inline uint16_t in16(uint16_t p) { return (uint16_t)ata_ports[p]; }
#else
static inline void out8(uint16_t p, uint8_t v) { __asm__ volatile("outb %0,%1" : : "a"(v),"Nd"(p)); }
static inline uint8_t in8(uint16_t p) { uint8_t v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline void out16(uint16_t p, uint16_t v) { __asm__ volatile("outw %0,%1" : : "a"(v),"Nd"(p)); }
static inline uint16_t in16(uint16_t p) { uint16_t v; __asm__ volatile("inw %1,%0":"=a"(v):"Nd"(p)); return v; }
#endif

static int ata_ready;
static uint32_t ata_sectors;
static uint8_t ata_drive;

static void ata_delay(void) {
    (void)in8(ATA_CTRL); (void)in8(ATA_CTRL);
    (void)in8(ATA_CTRL); (void)in8(ATA_CTRL);
}

static uint8_t ata_wait(uint32_t limit) {
    while (limit--) {
        uint8_t s = in8(ATA_IO + ATA_STATUS);
        if (!(s & ATA_SR_BSY)) return s;
    }
    return 0;
}

static int ata_identify(uint8_t drive) {
    uint16_t id[256];
    out8(ATA_IO + ATA_DEVICE, (uint8_t)(0xA0u | (drive ? 0x10u : 0)));
    ata_delay();
    out8(ATA_IO + ATA_SECCOUNT, 0);
    out8(ATA_IO + ATA_LBA0, 0);
    out8(ATA_IO + ATA_LBA1, 0);
    out8(ATA_IO + ATA_LBA2, 0);
    out8(ATA_IO + ATA_COMMAND, ATA_CMD_IDENTIFY);
    uint8_t s = in8(ATA_IO + ATA_STATUS);
    if (s == 0 || s == 0xFFu) return 0;
    s = ata_wait(1000000u);
    if (!s || (s & ATA_SR_ERR) || (s & ATA_SR_DF) || !(s & ATA_SR_DRQ)) return 0;
    for (int i = 0; i < 256; ++i) id[i] = in16(ATA_IO + ATA_DATA);
    ata_sectors = (uint32_t)id[60] | ((uint32_t)id[61] << 16);
    if (ata_sectors < 16u) return 0;
    ata_drive = drive;
    return 1;
}

static int ata_select(uint32_t lba) {
    if (!ata_ready || lba >= ata_sectors || lba > 0x0FFFFFFFu) return 0;
    out8(ATA_IO + ATA_DEVICE, (uint8_t)(0xE0u | (ata_drive ? 0x10u : 0) | ((lba >> 24) & 0x0Fu)));
    ata_delay();
    out8(ATA_IO + ATA_SECCOUNT, 1);
    out8(ATA_IO + ATA_LBA0, (uint8_t)lba);
    out8(ATA_IO + ATA_LBA1, (uint8_t)(lba >> 8));
    out8(ATA_IO + ATA_LBA2, (uint8_t)(lba >> 16));
    return 1;
}

void ata_init(void) {
    ata_ready = 0;
    ata_sectors = 0;
    ata_drive = 0;
    out8(ATA_CTRL, 0x02);
    if (ata_identify(0) || ata_identify(1)) ata_ready = 1;
}

int ata_available(void) { return ata_ready; }
uint32_t ata_sector_count(void) { return ata_sectors; }

int ata_read_sector(uint32_t lba, uint8_t* out) {
    if (!out || !ata_select(lba)) return 0;
    out8(ATA_IO + ATA_COMMAND, ATA_CMD_READ);
    uint8_t s = ata_wait(1000000u);
    if (!s || (s & ATA_SR_ERR) || (s & ATA_SR_DF) || !(s & ATA_SR_DRQ)) return 0;
    for (int i = 0; i < 256; ++i) {
        uint16_t v = in16(ATA_IO + ATA_DATA);
        out[i * 2] = (uint8_t)v;
        out[i * 2 + 1] = (uint8_t)(v >> 8);
    }
    return 1;
}

int ata_write_sector(uint32_t lba, const uint8_t* data) {
    if (!data || !ata_select(lba)) return 0;
    out8(ATA_IO + ATA_COMMAND, ATA_CMD_WRITE);
    uint8_t s = ata_wait(1000000u);
    if (!s || (s & ATA_SR_ERR) || (s & ATA_SR_DF) || !(s & ATA_SR_DRQ)) return 0;
    for (int i = 0; i < 256; ++i) {
        uint16_t v = (uint16_t)data[i * 2] | ((uint16_t)data[i * 2 + 1] << 8);
        out16(ATA_IO + ATA_DATA, v);
    }
    out8(ATA_IO + ATA_COMMAND, ATA_CMD_FLUSH);
    s = ata_wait(1000000u);
    return s && !(s & ATA_SR_ERR) && !(s & ATA_SR_DF);
}

int ata_storage_marker_present(uint32_t lba) {
    uint8_t block[ATA_SECTOR_SIZE] = {0};
    if (!ata_read_sector(lba, block)) return 0;
    uint32_t m = (uint32_t)block[0] | ((uint32_t)block[1] << 8) |
                 ((uint32_t)block[2] << 16) | ((uint32_t)block[3] << 24);
    return m == ATA_SIGNATURE && block[4] == 1;
}
