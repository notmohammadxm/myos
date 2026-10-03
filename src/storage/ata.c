#include "ata.h"
#include <stddef.h>

#define ATA_IO 0x1F0
#define ATA_CTRL 0x3F6
#define ATA_DATA 0
#define ATA_ERROR 1
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
#define ATA_SR_DRDY 0x40
#define ATA_SR_BSY 0x80

#define ATA_CMD_IDENTIFY 0xEC
#define ATA_CMD_READ 0x20
#define ATA_CMD_WRITE 0x30
#define ATA_CMD_FLUSH 0xE7

#define ATA_SIGNATURE 0x4D594F53u

#ifdef UNIT_TEST
static uint32_t ports[65536];
static inline void out8(uint16_t p,uint8_t v){ports[p]=(ports[p]&0xFFFFFF00u)|v;}
static inline uint8_t in8(uint16_t p){return (uint8_t)ports[p];}
static inline void out16(uint16_t p,uint16_t v){ports[p]=(ports[p]&0xFFFF0000u)|v;}
static inline uint16_t in16(uint16_t p){return (uint16_t)ports[p];}
#else
static inline void out8(uint16_t p,uint8_t v){__asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));}
static inline uint8_t in8(uint16_t p){uint8_t v;__asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p));return v;}
static inline void out16(uint16_t p,uint16_t v){__asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p));}
static inline uint16_t in16(uint16_t p){uint16_t v;__asm__ volatile("inw %1,%0":"=a"(v):"Nd"(p));return v;}
#endif

static int ready;
static uint32_t sectors;
static int last_drive;
static uint8_t identify_buf[512];

static void delay400ns(void){
    (void)in8(ATA_CTRL); (void)in8(ATA_CTRL); (void)in8(ATA_CTRL); (void)in8(ATA_CTRL);
}

static int wait_not_busy(uint32_t limit){
    while(limit--){
        uint8_t s=in8(ATA_IO+ATA_STATUS);
        if(!(s&ATA_SR_BSY)) return s;
    }
    return 0;
}

static int identify_drive(int slave){
    out8(ATA_IO+ATA_DEVICE,(uint8_t)(0xA0u|(slave?0x10u:0)));
    delay400ns();
    out8(ATA_IO+ATA_SECCOUNT,0);
    out8(ATA_IO+ATA_LBA0,0);
    out8(ATA_IO+ATA_LBA1,0);
    out8(ATA_IO+ATA_LBA2,0);
    out8(ATA_IO+ATA_COMMAND,ATA_CMD_IDENTIFY);
    uint8_t s=in8(ATA_IO+ATA_STATUS);
    if(s==0 || s==0xFFu) return 0;
    if(!wait_not_busy(1000000u)) return 0;
    if(in8(ATA_IO+ATA_LBA1)!=0 || in8(ATA_IO+ATA_LBA2)!=0) return 0;
    if(!(in8(ATA_IO+ATA_STATUS)&ATA_SR_DRQ)) return 0;
    for(int i=0;i<256;++i) {
        uint16_t w=in16(ATA_IO+ATA_DATA);
        identify_buf[i*2]=(uint8_t)w;
        identify_buf[i*2+1]=(uint8_t)(w>>8);
    }
    uint32_t lba28=(uint32_t)identify_buf[120] | ((uint32_t)identify_buf[121]<<8) |
                   ((uint32_t)identify_buf[122]<<16) | ((uint32_t)identify_buf[123]<<24);
    if(lba28<16u) return 0;
    sectors=lba28;
    last_drive=slave;
    return 1;
}

static int select_lba(uint32_t lba){
    if(!ready || lba>=sectors || lba>0x0FFFFFFFu) return 0;
    out8(ATA_IO+ATA_DEVICE,(uint8_t)(0xE0u|(last_drive?0x10u:0)|((lba>>24)&0x0Fu)));
    delay400ns();
    out8(ATA_IO+ATA_SECCOUNT,1);
    out8(ATA_IO+ATA_LBA0,(uint8_t)lba);
    out8(ATA_IO+ATA_LBA1,(uint8_t)(lba>>8));
    out8(ATA_IO+ATA_LBA2,(uint8_t)(lba>>16));
    return 1;
}

void ata_init(void){
    ready=0; sectors=0; last_drive=0;
    out8(ATA_CTRL,0x02);
    if(identify_drive(0)) {ready=1;return;}
    if(identify_drive(1)) {ready=1;return;}
}

int ata_available(void){return ready!=0;}
uint32_t ata_sector_count(void){return sectors;}

int ata_read_sector(uint32_t lba,uint8_t* out){
    if(!ready||!out||!select_lba(lba)) return 0;
    out8(ATA_IO+ATA_COMMAND,ATA_CMD_READ);
    uint8_t s=wait_not_busy(1000000u);
    if(!s||(s&ATA_SR_ERR)||(s&ATA_SR_DF)) return 0;
    for(int i=0;i<256;++i){
        uint16_t w;
        if(!(in8(ATA_IO+ATA_STATUS)&ATA_SR_DRQ)) return 0;
        w=in16(ATA_IO+ATA_DATA);
        out[i*2]=(uint8_t)w; out[i*2+1]=(uint8_t)(w>>8);
    }
    delay400ns();
    return 1;
}

int ata_write_sector(uint32_t lba,const uint8_t* data){
    if(!ready||!data||!select_lba(lba)) return 0;
    out8(ATA_IO+ATA_COMMAND,ATA_CMD_WRITE);
    uint8_t s=wait_not_busy(1000000u);
    if(!s||(s&ATA_SR_ERR)||(s&ATA_SR_DF)||(s&ATA_SR_DRQ)==0) return 0;
    for(int i=0;i<256;++i){
        uint16_t w=(uint16_t)data[i*2]|((uint16_t)data[i*2+1]<<8);
        out16(ATA_IO+ATA_DATA,w);
    }
    delay400ns();
    out8(ATA_IO+ATA_COMMAND,ATA_CMD_FLUSH);
    s=wait_not_busy(1000000u);
    return s && !(s&ATA_SR_ERR) && !(s&ATA_SR_DF);
}

int ata_storage_marker_present(uint32_t lba){
    uint8_t block[512];
    if(!ata_read_sector(lba,block)) return 0;
    uint32_t marker=(uint32_t)block[0]|((uint32_t)block[1]<<8)|
                    ((uint32_t)block[2]<<16)|((uint32_t)block[3]<<24);
    return marker==ATA_SIGNATURE && block[4]==1;
}
