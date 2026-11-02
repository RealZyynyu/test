#include <assert.h>
#include <stdint.h>
#include "../kernel/gdt.h"

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
    g.entries[4]^=1u;
    assert(!nova_gdt_valid(&g,&tss));
    nova_gdtr_build(&gdtr,NULL);
    assert(gdtr.limit==0&&gdtr.base==0);
    return 0;
}
