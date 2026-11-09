#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../kernel/gdt.h"
#include "../kernel/idt.h"

int main(void){
    struct nova_gdt g;
    struct nova_tss64 tss={0};
    struct nova_gdtr gdtr={0};
    assert(!nova_gdt_build(NULL,&tss));
    assert(!nova_gdt_build(&g,NULL));
    assert(nova_gdt_build(&g,&tss));
    assert(nova_gdt_valid(&g,&tss));
    assert(g.entries[0]==0);
    assert(g.entries[1]==NOVA_GDT_KERNEL_CODE);
    assert(g.entries[2]==NOVA_GDT_KERNEL_DATA);
    assert(g.entries[3]==NOVA_GDT_USER_DATA);
    assert(g.entries[4]==NOVA_GDT_USER_CODE);
    assert(nova_tss_descriptor_base(&g.tss)==(uint64_t)(uintptr_t)&tss);
    assert(nova_tss_descriptor_limit(&g.tss)==103u);
    nova_gdtr_build(&gdtr,&g);
    assert(gdtr.limit==sizeof(g)-1u);
    assert(gdtr.base==(uint64_t)(uintptr_t)&g);
    assert(nova_gdt_valid(&g,&tss));
    /* All four high-nibble flag bits are reserved for a 64-bit TSS. */
    for(uint8_t reserved=0x10u;reserved;
        reserved=(uint8_t)(reserved<<1)){
        g.tss.limit_high_flags=reserved;
        assert(!nova_gdt_valid(&g,&tss));
    }
    g.tss.limit_high_flags=0;
    assert(nova_gdt_valid(&g,&tss));
    g.entries[4]^=1u;
    assert(!nova_gdt_valid(&g,&tss));
    nova_gdtr_build(&gdtr,NULL);
    assert(gdtr.limit==0&&gdtr.base==0);

    /* IDT builders reject malformed selectors and gate attributes. */
    struct nova_idt_entry idt={0};
    struct nova_idt_entry saved={0};
    const uintptr_t handler=(uintptr_t)UINT64_C(0x123456789ABC);
    assert(nova_idt_entry_build(&idt,handler,0x08u,0u,0x8Eu));
    assert(nova_idt_entry_valid(&idt));
    assert(idt.segment==0x08u && idt.ist==0u && idt.flags==0x8Eu);
    assert(idt.offset_0_15==(uint16_t)handler);
    assert(idt.offset_16_31==(uint16_t)(handler>>16));
    assert(idt.offset_32_63==(uint32_t)((uint64_t)handler>>32));
    assert(idt.reserved==0u);
    saved=idt;
    assert(!nova_idt_entry_build(NULL,handler,0x08u,0u,0x8Eu));
    assert(!nova_idt_entry_build(&idt,0u,0x08u,0u,0x8Eu));
    assert(!nova_idt_entry_build(&idt,handler,0u,0u,0x8Eu));
    assert(!nova_idt_entry_build(&idt,handler,0x0Bu,0u,0x8Eu));
    assert(!nova_idt_entry_build(&idt,handler,0x08u,8u,0x8Eu));
    assert(!nova_idt_entry_build(&idt,handler,0x08u,0u,0x0Eu));
    assert(!nova_idt_entry_build(&idt,handler,0x08u,0u,0x9Eu));
    /* DPL 1, 2 and 3 gates must not become user-callable entry points. */
    assert(!nova_idt_entry_build(&idt,handler,0x08u,0u,0xAEu));
    assert(!nova_idt_entry_build(&idt,handler,0x08u,0u,0xCEu));
    assert(!nova_idt_entry_build(&idt,handler,0x08u,0u,0xEEu));
    assert(!nova_idt_entry_build(&idt,handler,0x08u,0u,0x8Cu));
    assert(memcmp(&idt,&saved,sizeof(idt))==0);
    assert(nova_idt_entry_build(&idt,handler,0x08u,7u,0x8Fu));
    assert(nova_idt_entry_valid(&idt));
    idt.ist=0x08u;
    assert(!nova_idt_entry_valid(&idt));
    idt=saved;
    idt.reserved=1u;
    assert(!nova_idt_entry_valid(&idt));
    idt=saved;
    idt.flags=0x8Cu;
    assert(!nova_idt_entry_valid(&idt));
    assert(!nova_idt_entry_valid(NULL));
    return 0;
}
