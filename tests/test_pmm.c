/* Host-side PMM security regression tests. No boot, ISO or disk writes.
 * Uses ONLY tests/stubs/limine.h; the native kernel uses real Limine.
 */
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/pmm.h"

#define TEST_POOL_PAGES 32u
static _Alignas(4096) uint8_t emulated_ram[TEST_POOL_PAGES * 4096u];

static void reset_allocator(void) {
    nova_pmm=(struct nova_page_manager){0};
    memset(emulated_ram,0x7A,sizeof(emulated_ram));
}
static struct limine_hhdm_response mapped_to(uint64_t physical) {
    struct limine_hhdm_response hhdm={0};
    assert((uintptr_t)emulated_ram>=physical);
    hhdm.offset=(uint64_t)((uintptr_t)emulated_ram-physical);
    return hhdm;
}
static void check_reject_overflow_and_reserved(void) {
    reset_allocator();
    struct limine_memmap_entry overflow={
        .base=UINT64_MAX-2048u,.length=0x10000u,.type=LIMINE_MEMMAP_USABLE};
    struct limine_memmap_entry reserved={
        .base=0x100000u,.length=100u*4096u,.type=LIMINE_MEMMAP_RESERVED};
    struct limine_memmap_entry too_short={
        .base=0x300000u,.length=7u*4096u,.type=LIMINE_MEMMAP_USABLE};
    struct limine_memmap_entry valid={
        .base=0x200000u,.length=20u*4096u,.type=LIMINE_MEMMAP_USABLE};
    struct limine_memmap_entry *regions[]={&overflow,&reserved,&too_short,&valid};
    struct limine_memmap_response mm={
        .entry_count=4,.entries=regions};
    struct limine_hhdm_response hhdm=mapped_to(0x200000u);
    assert(nova_pmm_init(&mm,&hhdm));
    assert(nova_pmm.initialized);
    assert(nova_pmm.phys_base==0x200000u);
    assert(nova_pmm.pages==20u && nova_pmm.in_use==0u);
    assert(nova_free_pages()==20u);
    assert(!nova_pmm_init(&mm,&hhdm)); /* reinit would lose ownership */
    assert(nova_pmm_self_test());
    assert(nova_free_pages()==20u);
}
static void check_allocation_ownership_and_wipe(void) {
    /* Uses the 20-page live pool from the preceding test. */
    uint8_t *first=(uint8_t*)nova_page_alloc();
    assert(first==emulated_ram);
    uint64_t phys=0;
    assert(nova_pmm_physical(first,&phys)&&phys==0x200000u);
    assert(!nova_pmm_physical(first+1,&phys));
    assert(!nova_pmm_physical(NULL,&phys));
    assert(!nova_pmm_physical(first,NULL));
    assert(!nova_page_free(first+1));
    assert(!nova_page_free((void*)(uintptr_t)0x1234));
    assert(nova_pmm.in_use==1u);
    memset(first,0xD3,4096);
    assert(nova_page_free(first));
    for(unsigned i=0;i<4096;i++)assert(first[i]==0);
    assert(!nova_page_free(first));
    assert(!nova_pmm_physical(first,&phys)); /* unallocated frame */
    first=(uint8_t*)nova_page_alloc();
    assert(first==emulated_ram);
    for(unsigned i=0;i<4096;i++)assert(first[i]==0);
    uint8_t *pages[20]={first};
    for(unsigned i=1;i<20;i++){
        pages[i]=(uint8_t*)nova_page_alloc();
        assert(pages[i]!=NULL && pages[i]!=pages[i-1]);
        pages[i][17]=(uint8_t)i;
    }
    assert(nova_free_pages()==0);
    assert(nova_page_alloc()==NULL);
    assert(!nova_page_free(emulated_ram+21u*4096u));
    for(unsigned i=0;i<20;i++)
        assert(nova_page_free(pages[i]));
    assert(nova_free_pages()==20u && nova_pmm.in_use==0);
    for(unsigned i=0;i<20u*4096u;i++)assert(emulated_ram[i]==0);
}
static void check_malformed_mappings_fail_closed(void) {
    reset_allocator();
    struct limine_memmap_entry broken={
        .base=UINT64_MAX-1024u,.length=8192u,.type=LIMINE_MEMMAP_USABLE};
    struct limine_memmap_entry *regions[]={&broken};
    struct limine_memmap_response mm={.entry_count=1,.entries=regions};
    struct limine_hhdm_response hhdm=mapped_to(0x200000u);
    assert(!nova_pmm_init(&mm,&hhdm));
    assert(!nova_pmm.initialized && nova_page_alloc()==NULL);
    assert(!nova_page_free(emulated_ram));
    assert(!nova_pmm_physical(emulated_ram,&(uint64_t){0}));
    assert(!nova_pmm_init(NULL,&hhdm));
    assert(!nova_pmm_init(&mm,NULL));
    struct limine_memmap_response no_regions={.entry_count=1,.entries=NULL};
    assert(!nova_pmm_init(&no_regions,&hhdm));
    /* Reject a pool whose last byte would overflow the HHDM mapping. */
    broken.base=0x200000u;
    broken.length=16u*4096u;
    hhdm.offset=UINTPTR_MAX-0x200000u;
    assert(!nova_pmm_init(&mm,&hhdm));
    assert(!nova_pmm.initialized);
    /* Sub-page region and fewer than eight pages are insufficient. */
    hhdm=mapped_to(0x200000u);
    broken.length=4095u;
    assert(!nova_pmm_init(&mm,&hhdm));
    broken.length=7u*4096u;
    assert(!nova_pmm_init(&mm,&hhdm));
    /* An unaligned start can safely be rounded to the next 4K boundary. */
    broken.base=0x200001u;
    broken.length=11u*4096u;
    hhdm=mapped_to(0x201000u);
    assert(nova_pmm_init(&mm,&hhdm));
    assert(nova_pmm.phys_base==0x201000u&&nova_pmm.pages==10);
    assert(nova_pmm_self_test());
    reset_allocator();
}
int main(void){
    check_reject_overflow_and_reserved();
    check_allocation_ownership_and_wipe();
    check_malformed_mappings_fail_closed();
    puts("PMM: overflowed memmap/HHDM rejected, ownership checked, freed RAM zeroed, and exhaustion safe.");
    return 0;
}
