/* Legacy PCI configuration-space inventory (read only).
 * Enumerates configuration headers; does not initialize/drive hardware.
 */
#ifndef NOVA_PCI_H
#define NOVA_PCI_H
#include <stdint.h>
struct nova_pci_info {
    uint32_t devices,network,storage,display,usb;
};
static struct nova_pci_info nova_pci;
static void nova_out32(uint16_t port,uint32_t value){
    __asm__ volatile ("outl %0,%1"::"a"(value),"Nd"(port));
}
static uint32_t nova_in32(uint16_t port){
    uint32_t value;
    __asm__ volatile("inl %1,%0":"=a"(value):"Nd"(port));
    return value;
}
static uint32_t nova_pci_read(uint8_t bus,uint8_t device,uint8_t fn,uint8_t reg){
    uint32_t addr=0x80000000u|((uint32_t)bus<<16)|((uint32_t)device<<11)
               |((uint32_t)fn<<8)|((uint32_t)(reg&0xFC));
    nova_out32(0xCF8,addr);
    return nova_in32(0xCFC);
}
static void nova_pci_scan(void){
    nova_pci=(struct nova_pci_info){0};
    for(unsigned bus=0;bus<8;bus++){
        for(unsigned dev=0;dev<32;dev++){
            uint32_t id=nova_pci_read((uint8_t)bus,(uint8_t)dev,0,0);
            if(id==0xFFFFFFFFu || (id&0xFFFF)==0xFFFF)continue;
            uint32_t ht=nova_pci_read((uint8_t)bus,(uint8_t)dev,0,0x0C);
            unsigned funcs=((ht>>16)&0x80)?8:1;
            for(unsigned fun=0;fun<funcs;fun++){
                id=nova_pci_read((uint8_t)bus,(uint8_t)dev,(uint8_t)fun,0);
                if(id==0xFFFFFFFFu || (id&0xFFFF)==0xFFFF)continue;
                uint32_t cc=nova_pci_read((uint8_t)bus,(uint8_t)dev,(uint8_t)fun,8);
                unsigned cls=(cc>>24)&0xFF,sub=(cc>>16)&0xFF;
                nova_pci.devices++;
                if(cls==2)nova_pci.network++;
                if(cls==1)nova_pci.storage++;
                if(cls==3)nova_pci.display++;
                if(cls==12&&sub==3)nova_pci.usb++;
            }
        }
    }
}
#endif
