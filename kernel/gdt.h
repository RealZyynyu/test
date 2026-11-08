/* NOVA x86-64 GDT construction helpers — NOT installed.
 *
 * The bootloader's active GDT remains untouched. Replacing it safely requires
 * a separately audited far control transfer, segment reload, TSS load and
 * exception-stack setup. These helpers only build and validate a candidate.
 */
#ifndef NOVA_GDT_H
#define NOVA_GDT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "tss.h"

#define NOVA_GDT_KERNEL_CODE_SELECTOR 0x08u
#define NOVA_GDT_KERNEL_DATA_SELECTOR 0x10u
#define NOVA_GDT_USER_DATA_SELECTOR   0x1Bu
#define NOVA_GDT_USER_CODE_SELECTOR   0x23u
#define NOVA_GDT_TSS_SELECTOR         0x28u
#define NOVA_GDT_ENTRIES 7u

struct nova_gdt {
    uint64_t entries[5];
    struct nova_tss_descriptor tss;
} __attribute__((packed));

struct nova_gdtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

_Static_assert(sizeof(struct nova_gdt)==56u,"GDT size mismatch");
_Static_assert(sizeof(struct nova_gdtr)==10u,"GDTR size mismatch");
_Static_assert(offsetof(struct nova_gdt,tss)==40u,"TSS GDT slot mismatch");

/* Flat 4-GiB data and 64-bit code segments. Long-mode code descriptors use
 * L=1, D=0; data descriptors use L=0. User selectors carry RPL=3.
 */
#define NOVA_GDT_KERNEL_CODE UINT64_C(0x00AF9A000000FFFF)
#define NOVA_GDT_KERNEL_DATA UINT64_C(0x00CF92000000FFFF)
#define NOVA_GDT_USER_DATA   UINT64_C(0x00CFF2000000FFFF)
#define NOVA_GDT_USER_CODE   UINT64_C(0x00AFFA000000FFFF)

static inline bool nova_gdt_build(struct nova_gdt *g,
                                  const struct nova_tss64 *tss){
    if(!g||!tss)return false;
    *g=(struct nova_gdt){0};
    g->entries[1]=NOVA_GDT_KERNEL_CODE;
    g->entries[2]=NOVA_GDT_KERNEL_DATA;
    g->entries[3]=NOVA_GDT_USER_DATA;
    g->entries[4]=NOVA_GDT_USER_CODE;
    return nova_tss_descriptor_build(&g->tss,(uintptr_t)tss,
                                     (uint32_t)sizeof(*tss)-1u);
}
static inline void nova_gdtr_build(struct nova_gdtr *gdtr,
                                   const struct nova_gdt *g){
    if(!gdtr)return;
    if(!g){gdtr->limit=0;gdtr->base=0;return;}
    gdtr->limit=(uint16_t)(sizeof(*g)-1u);
    gdtr->base=(uint64_t)(uintptr_t)g;
}
static inline bool nova_gdt_valid(const struct nova_gdt *g,
                                  const struct nova_tss64 *tss){
    if(!g||!tss||g->entries[0]!=0||
       g->entries[1]!=NOVA_GDT_KERNEL_CODE||
       g->entries[2]!=NOVA_GDT_KERNEL_DATA||
       g->entries[3]!=NOVA_GDT_USER_DATA||
       g->entries[4]!=NOVA_GDT_USER_CODE||
       g->tss.type_attr!=0x89u||
       (g->tss.limit_high_flags&0xF0u)!=0||
       nova_tss_descriptor_base(&g->tss)!=(uint64_t)(uintptr_t)tss||
       nova_tss_descriptor_limit(&g->tss)!=sizeof(*tss)-1u||
       g->tss.reserved!=0)return false;
    return true;
}
#endif
