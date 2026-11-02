#include <assert.h>
#include <stdint.h>
#include "../kernel/tss.h"

int main(void){
    struct nova_tss64 tss;
    assert(nova_tss_init(&tss));
    assert(sizeof(tss)==104u);
    assert(tss.iomap_base==104u);
    assert(tss.rsp0==0u && tss.rsp1==0u && tss.rsp2==0u);
    assert(tss.ist1==0u && tss.ist2==0u && tss.ist3==0u);
    assert(tss.ist4==0u && tss.ist5==0u && tss.ist6==0u && tss.ist7==0u);
    assert(!nova_tss_init(NULL));

    struct nova_tss_descriptor d;
    uintptr_t base=(uintptr_t)UINT64_C(0x123456789ABC);
    assert(nova_tss_descriptor_build(&d,base,sizeof(tss)-1u));
    assert(d.type_attr==0x89u);
    assert(nova_tss_descriptor_base(&d)==(uint64_t)base);
    assert(nova_tss_descriptor_limit(&d)==sizeof(tss)-1u);
    assert(d.reserved==0u);

    assert(!nova_tss_descriptor_build(NULL,base,103u));
    assert(!nova_tss_descriptor_build(&d,base,0x100000u));
    assert(nova_tss_descriptor_base(NULL)==0u);
    assert(nova_tss_descriptor_limit(NULL)==0u);
    return 0;
}
