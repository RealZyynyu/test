/* NOVA x86-64 IDT gate construction and validation helpers.
 * These helpers only build in-memory descriptors. They do not execute LIDT,
 * enable interrupts, or activate an IST stack.
 */
#ifndef NOVA_IDT_H
#define NOVA_IDT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct nova_idt_entry {
    uint16_t offset_0_15;
    uint16_t segment;
    uint8_t ist;
    uint8_t flags;
    uint16_t offset_16_31;
    uint32_t offset_32_63;
    uint32_t reserved;
} __attribute__((packed));

_Static_assert(sizeof(struct nova_idt_entry)==16u,
               "x86-64 IDT gate must be 16 bytes");

static inline bool nova_idt_gate_flags_valid(uint8_t flags){
    uint8_t type=(uint8_t)(flags&0x0Fu);
    /* Kernel IDT gates stay DPL 0; user-callable software gates are
     * intentionally unsupported until a separate ABI is designed.
     */
    return (flags&0x80u)!=0u && (flags&0x70u)==0u &&
           (type==0x0Eu || type==0x0Fu);
}

static inline bool nova_idt_entry_valid(const struct nova_idt_entry *entry){
    if(!entry || !entry->segment || (entry->segment&0x07u)!=0u ||
       (entry->ist&0xF8u)!=0u ||
       !nova_idt_gate_flags_valid(entry->flags) || entry->reserved!=0u)
        return false;
    uint64_t address=(uint64_t)entry->offset_0_15|
        ((uint64_t)entry->offset_16_31<<16)|
        ((uint64_t)entry->offset_32_63<<32);
    return address!=0u;
}

/* Build a present 64-bit interrupt (0xE) or trap (0xF) gate.
 * The selector must identify a ring-0 GDT code segment. IST is the raw
 * three-bit index; callers must not use a nonzero index until its TSS stack
 * has been installed and mapped.
 */
static inline bool nova_idt_entry_build(struct nova_idt_entry *entry,
                                        uintptr_t handler,
                                        uint16_t segment,
                                        uint8_t ist,uint8_t flags){
    if(!entry || !handler || !segment || (segment&0x07u)!=0u ||
       ist>7u || !nova_idt_gate_flags_valid(flags))return false;
    uint64_t address=(uint64_t)handler;
    struct nova_idt_entry candidate={0};
    candidate.offset_0_15=(uint16_t)address;
    candidate.segment=segment;
    candidate.ist=ist;
    candidate.flags=flags;
    candidate.offset_16_31=(uint16_t)(address>>16);
    candidate.offset_32_63=(uint32_t)(address>>32);
    if(!nova_idt_entry_valid(&candidate))return false;
    *entry=candidate;
    return true;
}
#endif
