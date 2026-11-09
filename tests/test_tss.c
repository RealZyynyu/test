#include <assert.h>
#include <stdint.h>
#include <string.h>
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
    assert(nova_tss_valid(&tss));
    assert(!nova_tss_valid(NULL));

    /* IST values may be configured while the TSS remains deny-all-I/O. */

    /* IST entries are configurable data only; no IDT gate or TR is loaded. */
    assert(nova_tss_set_ist(&tss,1u,UINT64_C(0x1000)));
    assert(nova_tss_set_ist(&tss,7u,UINT64_C(0x7000)));
    assert(tss.ist1==UINT64_C(0x1000) && tss.ist7==UINT64_C(0x7000));
    assert(nova_tss_set_ist(&tss,2u,UINT64_C(0x2000)));
    assert(nova_tss_set_ist(&tss,3u,UINT64_C(0x3000)));
    assert(nova_tss_set_ist(&tss,4u,UINT64_C(0x4000)));
    assert(nova_tss_set_ist(&tss,5u,UINT64_C(0x5000)));
    assert(nova_tss_set_ist(&tss,6u,UINT64_C(0x6000)));
    assert(tss.ist1==UINT64_C(0x1000));
    assert(tss.ist2==UINT64_C(0x2000));
    assert(tss.ist3==UINT64_C(0x3000));
    assert(tss.ist4==UINT64_C(0x4000));
    assert(tss.ist5==UINT64_C(0x5000));
    assert(tss.ist6==UINT64_C(0x6000));
    assert(tss.ist7==UINT64_C(0x7000));
    assert(nova_tss_valid(&tss));

    /* Reserved fields and the absent I/O-bitmap offset are mandatory. */
    struct nova_tss64 invalid=tss;
    invalid.reserved0=1u;
    assert(!nova_tss_valid(&invalid));
    invalid=tss; invalid.reserved1=1u;
    assert(!nova_tss_valid(&invalid));
    invalid=tss; invalid.reserved2=1u;
    assert(!nova_tss_valid(&invalid));
    invalid=tss; invalid.reserved3=1u;
    assert(!nova_tss_valid(&invalid));
    invalid=tss; invalid.iomap_base=0u;
    assert(!nova_tss_valid(&invalid));

    /* Rejected updates must leave the complete TSS unchanged. */
    struct nova_tss64 before_invalid=tss;
    assert(!nova_tss_set_ist(NULL,1u,UINT64_C(0x1000)));
    assert(!nova_tss_set_ist(&tss,0u,UINT64_C(0x1000)));
    assert(!nova_tss_set_ist(&tss,8u,UINT64_C(0x1000)));
    assert(!nova_tss_set_ist(&tss,2u,0u));
    assert(!nova_tss_set_ist(&tss,2u,UINT64_C(0x1001)));
    assert(memcmp(&tss,&before_invalid,sizeof(tss))==0);

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
