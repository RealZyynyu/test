/* NOVA read-only legacy primary-master ATA PIO driver.
 * This is a deliberately restricted prototype for emulated IDE disks.
 * NO writes are supported, and NO installation/formatting routines exist.
 * Does not support SATA/AHCI, NVMe, USB disks, ATAPI or LBA48.
 * Single CPU, no locking; uses bounded polling to avoid endless waits.
 */
#ifndef NOVA_ATA_H
#define NOVA_ATA_H
#include <stdint.h>
#include <stdbool.h>
#define NOVA_ATA_IO 0x1F0u
struct nova_ata_info {
    bool present, lba28;
    uint32_t sectors;
};
static struct nova_ata_info nova_ata;
static void nova_ata_out(uint16_t port,uint8_t v){
    __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(port));
}
static uint8_t nova_ata_in(uint16_t port){
    uint8_t v;
    __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(port));return v;
}
static uint16_t nova_ata_word(uint16_t port){
    uint16_t v;
    __asm__ volatile("inw %1,%0":"=a"(v):"Nd"(port));return v;
}
static void nova_ata_delay(void){
    (void)nova_ata_in(0x3F6);
    (void)nova_ata_in(0x3F6);
    (void)nova_ata_in(0x3F6);
    (void)nova_ata_in(0x3F6);
}
static bool nova_ata_wait_ready(void){
    for(unsigned n=0;n<200000u;n++){
        uint8_t s=nova_ata_in(0x1F7);
        if(s==0xFF || s==0)return false;
        if(!(s&0x80)) {
            if((s&0x21)!=0)return false; // Device fault or error
            if(s&0x08)return true;        // DRQ set
        }
    }
    return false;
}
static bool nova_ata_probe(void){
    nova_ata=(struct nova_ata_info){0};
    // Program IDE master/slave selection, but never issue a WRITE command.
    nova_ata_out(0x1F6,0xA0);
    nova_ata_delay();
    nova_ata_out(0x1F2,0);
    nova_ata_out(0x1F3,0);
    nova_ata_out(0x1F4,0);
    nova_ata_out(0x1F5,0);
    nova_ata_out(0x1F7,0xEC); // IDENTIFY DEVICE (read-only)
    uint8_t status=nova_ata_in(0x1F7);
    if(!status || status==0xFF)return false;
    if(nova_ata_in(0x1F4) || nova_ata_in(0x1F5))return false;
    if(!nova_ata_wait_ready())return false;
    uint16_t words[256];
    for(int i=0;i<256;i++)words[i]=nova_ata_word(NOVA_ATA_IO);
    nova_ata.lba28=(words[49]&(1u<<9))!=0;
    nova_ata.sectors=(uint32_t)words[60]|((uint32_t)words[61]<<16);
    nova_ata.present=nova_ata.lba28 && nova_ata.sectors>0;
    return nova_ata.present;
}
static bool nova_ata_read_sector(uint32_t lba,uint8_t out[512]){
    if(!nova_ata.present||!out||lba>=nova_ata.sectors||
       lba>=0x10000000u)return false;
    nova_ata_out(0x1F6,(uint8_t)(0xE0|((lba>>24)&15)));
    nova_ata_delay();
    nova_ata_out(0x1F2,1);
    nova_ata_out(0x1F3,(uint8_t)lba);
    nova_ata_out(0x1F4,(uint8_t)(lba>>8));
    nova_ata_out(0x1F5,(uint8_t)(lba>>16));
    nova_ata_out(0x1F7,0x20); // READ SECTORS, never writes
    if(!nova_ata_wait_ready())return false;
    for(unsigned i=0;i<256;i++){
        uint16_t word=nova_ata_word(NOVA_ATA_IO);
        out[2*i]=(uint8_t)word;
        out[2*i+1]=(uint8_t)(word>>8);
    }
    return true;
}
static bool nova_ata_readonly_selftest(void){
    uint8_t sector[512];
    if(!nova_ata_read_sector(0,sector))return false;
    // Only validate that the controller can fetch a sector. This
    // intentionally does not modify the drive or assume it has an MBR.
    return true;
}
#endif
