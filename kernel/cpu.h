/* x86 CPUID feature discovery. This does not constitute a driver stack. */
#ifndef NOVA_CPU_H
#define NOVA_CPU_H
#include <stdint.h>
#include <stdbool.h>
struct nova_cpu_info {
    char vendor[13];
    bool long_mode, sse2, apic, xsave, hypervisor;
    uint32_t max_basic_leaf;
};
static struct nova_cpu_info nova_cpu;
static void nova_cpuid(uint32_t leaf,uint32_t sub,
                       uint32_t *a,uint32_t *b,uint32_t *c,uint32_t *d){
    __asm__ volatile("cpuid":"=a"(*a),"=b"(*b),"=c"(*c),"=d"(*d):
                     "a"(leaf),"c"(sub));
}
static bool nova_cpu_detect(void){
    uint32_t a,b,c,d;
    nova_cpuid(0,0,&a,&b,&c,&d);
    nova_cpu.max_basic_leaf=a;
    uint32_t vendor[3]={b,d,c};
    for(unsigned i=0;i<12;i++)
        nova_cpu.vendor[i]=(char)(vendor[i/4]>>(8*(i%4)));
    nova_cpu.vendor[12]=0;
    if(a>=1){
        nova_cpuid(1,0,&a,&b,&c,&d);
        nova_cpu.sse2=(d&(1u<<26))!=0;
        nova_cpu.apic=(d&(1u<<9))!=0;
        nova_cpu.xsave=(c&(1u<<26))!=0;
        nova_cpu.hypervisor=(c&(1u<<31))!=0;
    }
    nova_cpuid(0x80000000u,0,&a,&b,&c,&d);
    if(a>=0x80000001u){
        nova_cpuid(0x80000001u,0,&a,&b,&c,&d);
        nova_cpu.long_mode=(d&(1u<<29))!=0;
    }
    return nova_cpu.vendor[0]!=0&&nova_cpu.long_mode;
}
#endif
