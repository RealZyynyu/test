/* NOVA Native x86-64 interrupt descriptor table, 8259 PIC and PIT.
 * This is a single-CPU legacy-mode timer driver for QEMU/VirtualBox.
 * It does NOT implement APIC, SMP, privilege switching, user processes,
 * preemptive scheduling, or a production-grade exception handler.
 */
#ifndef NOVA_INTERRUPTS_H
#define NOVA_INTERRUPTS_H
#include <stdint.h>
#include <stdbool.h>
struct nova_idt_entry {
    uint16_t offset_0_15;
    uint16_t segment;
    uint8_t ist;
    uint8_t flags;
    uint16_t offset_16_31;
    uint32_t offset_32_63;
    uint32_t reserved;
} __attribute__((packed));
struct nova_idtr {
    uint16_t limit;
    uint64_t address;
} __attribute__((packed));
static struct nova_idt_entry nova_idt[256];
static struct nova_idtr nova_idtr_data;
static uint16_t nova_kernel_cs=0;
static volatile uint64_t nova_timer_ticks=0;
static bool nova_idt_ready=false;
extern void nova_irq_timer_stub(void);
extern void nova_irq_master_stub(void);
extern void nova_irq_slave_stub(void);
extern void nova_exception_stub(void);
static void nova_interrupt_out(uint16_t port,uint8_t val){
    __asm__ volatile("outb %0,%1"::"a"(val),"Nd"(port));
}
static inline void nova_interrupt_wait(void){
    nova_interrupt_out(0x80,0);
}
static void nova_idt_set(unsigned vec,void (*handler)(void)){
    uintptr_t addr=(uintptr_t)handler;
    struct nova_idt_entry *g=&nova_idt[vec];
    g->offset_0_15=(uint16_t)addr;
    g->segment=nova_kernel_cs; // Current Limine-provided kernel CS
    g->ist=0;
    g->flags=0x8e; // ring 0, present, 64-bit interrupt gate
    g->offset_16_31=(uint16_t)(addr>>16);
    g->offset_32_63=(uint32_t)(addr>>32);
    g->reserved=0;
}
static void nova_pic_remap(void){
    // Initialize 8259s and use offsets 32 and 40.
    nova_interrupt_out(0x20,0x11);nova_interrupt_wait();
    nova_interrupt_out(0xA0,0x11);nova_interrupt_wait();
    nova_interrupt_out(0x21,0x20);nova_interrupt_wait();
    nova_interrupt_out(0xA1,0x28);nova_interrupt_wait();
    nova_interrupt_out(0x21,0x04);nova_interrupt_wait();
    nova_interrupt_out(0xA1,0x02);nova_interrupt_wait();
    nova_interrupt_out(0x21,0x01);nova_interrupt_wait();
    nova_interrupt_out(0xA1,0x01);nova_interrupt_wait();
    // Poll existing PS/2 input; IRQ1 and IRQ12 stay masked.
    nova_interrupt_out(0x21,0xFE);
    nova_interrupt_out(0xA1,0xFF);
}
static bool nova_interrupts_init(void){
    __asm__ volatile("cli");
    __asm__ volatile("mov %%cs, %0":"=r"(nova_kernel_cs));
    for(unsigned i=0;i<256;i++)nova_idt_set(i,nova_exception_stub);
    for(unsigned i=32;i<40;i++)nova_idt_set(i,nova_irq_master_stub);
    for(unsigned i=40;i<48;i++)nova_idt_set(i,nova_irq_slave_stub);
    nova_idt_set(32,nova_irq_timer_stub);
    nova_idtr_data.limit=sizeof(nova_idt)-1;
    nova_idtr_data.address=(uint64_t)(uintptr_t)&nova_idt[0];
    __asm__ volatile("lidt %0"::"m"(nova_idtr_data):"memory");
    nova_pic_remap();
    // 100Hz PIT channel 0, mode 3 square wave, divisor 11931.
    const uint16_t divisor=11932;
    nova_interrupt_out(0x43,0x36);
    nova_interrupt_out(0x40,(uint8_t)divisor);
    nova_interrupt_out(0x40,(uint8_t)(divisor>>8));
    nova_idt_ready=true;
    return true;
}
static void nova_interrupts_enable(void){
    if(nova_idt_ready)__asm__ volatile("sti");
}
#endif
