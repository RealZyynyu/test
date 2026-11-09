/* NOVA x86-64 Task State Segment (TSS) layout and descriptor helpers.
 * This file defines data structures only. It does NOT load TR, replace the
 * bootloader GDT, install IST stacks, or enable ring-3 execution.
 */
#ifndef NOVA_TSS_H
#define NOVA_TSS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct nova_tss64 {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

/* Initialize a dormant TSS. iomap_base points one byte past the
 * descriptor limit, disabling the optional I/O-permission bitmap: all
 * port-I/O permissions remain denied when this TSS is eventually loaded.
 * Stack pointers and IST entries deliberately remain zero until real,
 * mapped kernel stacks have been allocated and audited.
 */
static inline bool nova_tss_init(struct nova_tss64 *tss){
    if(!tss)return false;
    *tss=(struct nova_tss64){0};
    tss->iomap_base=(uint16_t)sizeof(*tss);
    return true;
}

/* Configure one-based IST slot 1..7 with a nonzero, 16-byte-aligned
 * stack top. The caller remains responsible for mapping and reserving the
 * stack before this value is ever used by a live IDT gate.
 */
static inline bool nova_tss_set_ist(struct nova_tss64 *tss,
                                    unsigned slot,uint64_t stack_top){
    if(!tss || slot<1u || slot>7u || !stack_top ||
       (stack_top&15u)!=0)return false;
    switch(slot){
        case 1:tss->ist1=stack_top;break;
        case 2:tss->ist2=stack_top;break;
        case 3:tss->ist3=stack_top;break;
        case 4:tss->ist4=stack_top;break;
        case 5:tss->ist5=stack_top;break;
        case 6:tss->ist6=stack_top;break;
        case 7:tss->ist7=stack_top;break;
        default:return false;
    }
    return true;
}

/* Validate the dormant, deny-all-I/O TSS representation. The I/O bitmap
 * is intentionally absent: iomap_base must remain one byte beyond the
 * 104-byte TSS limit, and all architecturally reserved fields stay zero.
 */
static inline bool nova_tss_stack_top_valid(uint64_t stack_top){
    return stack_top==0u || (stack_top&15u)==0u;
}
static inline bool nova_tss_valid(const struct nova_tss64 *tss){
    return tss && tss->reserved0==0u && tss->reserved1==0u &&
           tss->reserved2==0u && tss->reserved3==0u &&
           tss->iomap_base==sizeof(*tss) &&
           nova_tss_stack_top_valid(tss->rsp0) &&
           nova_tss_stack_top_valid(tss->ist1) &&
           nova_tss_stack_top_valid(tss->ist2) &&
           nova_tss_stack_top_valid(tss->ist3) &&
           nova_tss_stack_top_valid(tss->ist4) &&
           nova_tss_stack_top_valid(tss->ist5) &&
           nova_tss_stack_top_valid(tss->ist6) &&
           nova_tss_stack_top_valid(tss->ist7);
}

struct nova_tss_descriptor {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t type_attr;
    uint8_t limit_high_flags;
    uint8_t base_high;
    uint32_t base_upper;
    uint32_t reserved;
} __attribute__((packed));

_Static_assert(sizeof(struct nova_tss64)==104u,"x86-64 TSS must be 104 bytes");
_Static_assert(offsetof(struct nova_tss64,rsp0)==4u,"TSS RSP0 offset mismatch");
_Static_assert(offsetof(struct nova_tss64,ist1)==36u,"TSS IST1 offset mismatch");
_Static_assert(offsetof(struct nova_tss64,reserved2)==92u,"TSS reserved2 offset mismatch");
_Static_assert(offsetof(struct nova_tss64,reserved3)==100u,"TSS reserved3 offset mismatch");
_Static_assert(offsetof(struct nova_tss64,iomap_base)==102u,"TSS I/O map offset mismatch");
_Static_assert(sizeof(struct nova_tss_descriptor)==16u,"TSS descriptor must be 16 bytes");

/* Build an available 64-bit TSS descriptor (type 0x9, present, DPL 0).
 * The descriptor must occupy TWO consecutive GDT slots. It is not installed
 * or loaded by this helper.
 */
static inline bool nova_tss_descriptor_build(struct nova_tss_descriptor *out,
                                              uintptr_t base,uint32_t limit){
    /* A null base cannot identify a live TSS. Build into a temporary so
     * rejected inputs never leave a partially overwritten descriptor.
     */
    if(!out||!base||limit>0xFFFFFu)return false;
    uint64_t address=(uint64_t)base;
    struct nova_tss_descriptor candidate={0};
    candidate.limit_low=(uint16_t)limit;
    candidate.base_low=(uint16_t)address;
    candidate.base_mid=(uint8_t)(address>>16);
    candidate.type_attr=0x89u;
    candidate.limit_high_flags=(uint8_t)((limit>>16)&0x0Fu);
    candidate.base_high=(uint8_t)(address>>24);
    candidate.base_upper=(uint32_t)(address>>32);
    *out=candidate;
    return true;
}
static inline uint64_t nova_tss_descriptor_base(
    const struct nova_tss_descriptor *d){
    if(!d)return 0;
    return (uint64_t)d->base_low|
        ((uint64_t)d->base_mid<<16)|
        ((uint64_t)d->base_high<<24)|
        ((uint64_t)d->base_upper<<32);
}
static inline uint32_t nova_tss_descriptor_limit(
    const struct nova_tss_descriptor *d){
    if(!d)return 0;
    return (uint32_t)d->limit_low|
        ((uint32_t)(d->limit_high_flags&0x0Fu)<<16);
}
#endif
