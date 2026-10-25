/* NOVA OS Native CMOS/RTC time reader for emulated x86 PCs.
   UTC/local-time setting depends on firmware; not a trusted security clock.
   Single-core / non-concurrent and read-only, no alarms, no NTP support. */
#ifndef NOVA_RTC_H
#define NOVA_RTC_H
#include <stdint.h>
#include <stdbool.h>
struct nova_clock { unsigned year,month,day,hour,minute,second; bool valid; };
static void nova_rtc_out(uint16_t port,uint8_t data) {
    __asm__ volatile("outb %0,%1"::"a"(data),"Nd"(port));
}
static uint8_t nova_rtc_in(uint16_t port) {
    uint8_t data;
    __asm__ volatile("inb %1,%0":"=a"(data):"Nd"(port));return data;
}
static uint8_t nova_cmos_read(uint8_t index){
    nova_rtc_out(0x70,(uint8_t)(0x80|index));
    uint8_t value=nova_rtc_in(0x71);
    nova_rtc_out(0x70,0x0D); /* Reenable NMI; do not modify CMOS data */
    return value;
}
static unsigned nova_bcd(unsigned v){return (v&15)+10*((v>>4)&15);}
static struct nova_clock nova_read_clock(void){
    struct nova_clock t={0};
    // Wait a bounded number of polls for RTC update-in-progress to clear.
    for(int n=0;n<10000;n++)if(!(nova_cmos_read(0x0A)&0x80))break;
    unsigned sec=nova_cmos_read(0x00),min=nova_cmos_read(0x02);
    unsigned hour=nova_cmos_read(0x04),day=nova_cmos_read(0x07);
    unsigned month=nova_cmos_read(0x08),year=nova_cmos_read(0x09);
    unsigned regb=nova_cmos_read(0x0B);
    unsigned pm=hour&0x80;
    hour&=0x7F;
    if(!(regb&0x04)){
        sec=nova_bcd(sec);min=nova_bcd(min);hour=nova_bcd(hour);
        day=nova_bcd(day);month=nova_bcd(month);year=nova_bcd(year);
    }
    if(!(regb&0x02)){
        if(hour==12)hour=0;
        if(pm)hour+=12;
    }
    t.year=2000+year;t.month=month;t.day=day;
    t.hour=hour;t.minute=min;t.second=sec;
    t.valid=(year<=99&&month>=1&&month<=12&&day>=1&&day<=31&&
             hour<24&&min<60&&sec<60);
    return t;
}
static void nova_digits(char *out,unsigned val,unsigned digits){
    for(unsigned i=0;i<digits;i++){
        out[digits-i-1]=(char)('0'+val%10);
        val/=10;
    }
}
static bool nova_clock_string(char out[20]){
    struct nova_clock t=nova_read_clock();
    if(!t.valid){out[0]=0;return false;}
    nova_digits(out,t.year,4);out[4]='-';
    nova_digits(out+5,t.month,2);out[7]='-';
    nova_digits(out+8,t.day,2);out[10]=' ';
    nova_digits(out+11,t.hour,2);out[13]=':';
    nova_digits(out+14,t.minute,2);out[16]=':';
    nova_digits(out+17,t.second,2);out[19]=0;
    return true;
}
#endif
