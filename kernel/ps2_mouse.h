/* NOVA OS Native PS/2 mouse: polling-based controller and packet decoding.
 * Intended for VirtualBox/QEMU emulated PS/2 hardware. No IRQ/USB support.
 */
#ifndef NOVA_MOUSE_H
#define NOVA_MOUSE_H
#include <stdint.h>
#include <stdbool.h>
struct nova_mouse {
    int x, y;
    bool left, right;
    bool available;
    uint8_t packet[3];
    unsigned packet_index;
};
static struct nova_mouse mouse = {.x = 640, .y = 370};
static inline void ps2_out(uint16_t port, uint8_t value){
    __asm__ volatile ("outb %0,%1"::"a"(value),"Nd"(port));
}
static inline uint8_t ps2_in(uint16_t port){
    uint8_t v;
    __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(port));
    return v;
}
static bool ps2_wait_write(void) {
    for(unsigned n=0;n<100000;n++)if(!(ps2_in(0x64)&2))return true;
    return false;
}
static bool ps2_wait_read(uint8_t *value){
    for(unsigned n=0;n<100000;n++)if(ps2_in(0x64)&1){*value=ps2_in(0x60);return true;}
    return false;
}
static bool ps2_command(uint8_t c){
    if(!ps2_wait_write())return false;
    ps2_out(0x64,c);
    return true;
}
static bool ps2_data(uint8_t data){
    if(!ps2_wait_write())return false;
    ps2_out(0x60,data);
    return true;
}
static bool ps2_mouse_command(uint8_t cmd) {
    uint8_t response=0;
    if(!ps2_command(0xD4) || !ps2_data(cmd))return false;
    if(!ps2_wait_read(&response))return false;
    return response==0xFA;
}
static bool nova_mouse_init(void){
    uint8_t ctrl=0;
    // Drain any stale controller data before enabling the auxiliary port.
    for(int i=0;i<32 && (ps2_in(0x64)&1);i++)(void)ps2_in(0x60);
    if(!ps2_command(0xA8))return false;
    if(!ps2_command(0x20) || !ps2_wait_read(&ctrl))return false;
    ctrl=(uint8_t)(ctrl&~0x20u); // Enable PS/2 mouse clock
    ctrl=(uint8_t)(ctrl&~0x02u); // Keep IRQ12 off; kernel polls controller
    if(!ps2_command(0x60) || !ps2_data(ctrl))return false;
    if(!ps2_mouse_command(0xF6))return false; // Defaults
    if(!ps2_mouse_command(0xF4))return false; // Stream mode
    mouse.available=true;
    return true;
}
/* Returns true after a full 3-byte movement/button packet. */
static bool nova_mouse_byte(uint8_t value) {
    if(mouse.packet_index==0) {
        if((value&0x08)==0)return false;
        mouse.packet[0]=value;
        mouse.packet_index=1;
        return false;
    }
    mouse.packet[mouse.packet_index++]=value;
    if(mouse.packet_index!=3)return false;
    mouse.packet_index=0;
    uint8_t status=mouse.packet[0];
    if(!(status&0xC0)){
        int dx=(int)(int8_t)mouse.packet[1];
        int dy=(int)(int8_t)mouse.packet[2];
        mouse.x+=dx;
        mouse.y-=dy;
        if(mouse.x<0)mouse.x=0; if(mouse.x>1279)mouse.x=1279;
        if(mouse.y<0)mouse.y=0; if(mouse.y>799)mouse.y=799;
    }
    mouse.left=(status&1)!=0;
    mouse.right=(status&2)!=0;
    return true;
}
#endif
