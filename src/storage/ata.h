#ifndef MYOS_ATA_H
#define MYOS_ATA_H

#include <stdint.h>

void ata_init(void);
int ata_available(void);
uint32_t ata_sector_count(void);
int ata_read_sector(uint32_t lba, uint8_t* out);
int ata_write_sector(uint32_t lba, const uint8_t* data);
int ata_storage_marker_present(uint32_t lba);

#endif
