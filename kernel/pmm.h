/* NOVA physical page allocator (developer prototype).
 * Uses only Limine USABLE regions, maps through the Limine HHDM, and
 * manages a bounded 8 MiB pool. No virtual address spaces or isolation.
 * Never touches disk. Freestanding, single-core, no interrupt concurrency.
 * Rejects overflowed memory ranges and wipes frames on release.
 */
#ifndef NOVA_PMM_H
#define NOVA_PMM_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>
#define NOVA_PAGE_SIZE 4096ull
#define NOVA_PAGE_POOL_CAP 2048u
#define NOVA_BITMAP_SIZE ((NOVA_PAGE_POOL_CAP + 7u) / 8u)
struct nova_page_manager {
    uintptr_t hhdm_offset;
    uint64_t phys_base;
    uint32_t pages, in_use;
    uint8_t bitmap[NOVA_BITMAP_SIZE];
    bool initialized;
};
static struct nova_page_manager nova_pmm;
static uint64_t nova_align_up(uint64_t value,uint64_t align){
    return (value+(align-1))&~(align-1);
}
/* Reject damaged PMM bookkeeping BEFORE indexing the bitmap, wiping a
 * frame, or returning a physical address. This is a single-core consistency
 * check, not an authenticated ownership proof or a concurrency primitive.
 * The pool is deliberately small (at most 2048 frames); a full bitmap scan
 * is preferable to acting on inconsistent allocation metadata.
 */
static bool nova_pmm_metadata_valid(void){
    if(!nova_pmm.initialized || nova_pmm.pages<8u ||
       nova_pmm.pages>NOVA_PAGE_POOL_CAP ||
       nova_pmm.in_use>nova_pmm.pages ||
       (nova_pmm.phys_base&(NOVA_PAGE_SIZE-1u))!=0 ||
       (nova_pmm.hhdm_offset&(NOVA_PAGE_SIZE-1u))!=0)return false;
    const uint64_t span=(uint64_t)nova_pmm.pages*NOVA_PAGE_SIZE;
    if(nova_pmm.phys_base>UINTPTR_MAX-(span-1u))return false;
    const uintptr_t last=(uintptr_t)(nova_pmm.phys_base+span-1u);
    if(nova_pmm.hhdm_offset>UINTPTR_MAX-last)return false;

    const uint32_t used_bytes=(nova_pmm.pages+7u)/8u;
    uint32_t counted=0;
    for(uint32_t i=0;i<NOVA_BITMAP_SIZE;i++){
        const uint8_t bits=nova_pmm.bitmap[i];
        /* Unused bitmap bytes and tail bits must remain clear. */
        if(i>=used_bytes){
            if(bits)return false;
            continue;
        }
        if(i==nova_pmm.pages/8u && (nova_pmm.pages&7u)!=0){
            const uint8_t allowed=(uint8_t)((1u<<(nova_pmm.pages&7u))-1u);
            if((bits&(uint8_t)~allowed)!=0)return false;
        }
        for(unsigned bit=0;bit<8;bit++)
            counted+=(uint32_t)((bits>>bit)&1u);
    }
    return counted==nova_pmm.in_use;
}
static bool nova_pmm_init(volatile struct limine_memmap_response *mm,
                          volatile struct limine_hhdm_response *hhdm){
    /* Reinitializing an active allocator would lose all page ownership. */
    if(nova_pmm.initialized || !mm || !hhdm || !mm->entries)return false;
    uint64_t base=0;
    uint32_t pages=0;
    const uint64_t offset=(uint64_t)hhdm->offset;
    /* A misaligned HHDM would produce unaligned page pointers. */
    if((offset&(NOVA_PAGE_SIZE-1u))!=0)return false;
    for(uint64_t i=0;i<mm->entry_count;i++){
        struct limine_memmap_entry *entry=mm->entries[i];
        if(!entry || entry->type!=LIMINE_MEMMAP_USABLE ||
           entry->length<NOVA_PAGE_SIZE)continue;
        /* Reject malicious/corrupt BIOS region bounds before arithmetic. */
        if(entry->base>UINT64_MAX-entry->length ||
           entry->base>UINT64_MAX-(NOVA_PAGE_SIZE-1u))continue;
        const uint64_t end=entry->base+entry->length;
        const uint64_t first=nova_align_up(entry->base,NOVA_PAGE_SIZE);
        if(first>=end)continue;
        const uint64_t available=(end-first)/NOVA_PAGE_SIZE;
        const uint32_t count=available>NOVA_PAGE_POOL_CAP?
                             NOVA_PAGE_POOL_CAP:(uint32_t)available;
        if(count<=pages)continue;
        /* Entire HHDM mapped pool must fit uintptr_t without wraparound. */
        const uint64_t last=first+(uint64_t)count*NOVA_PAGE_SIZE-1u;
        if(last>UINTPTR_MAX || offset>UINTPTR_MAX-last)continue;
        pages=count;
        base=first;
        if(pages==NOVA_PAGE_POOL_CAP)break;
    }
    if(pages<8)return false;
    for(size_t i=0;i<NOVA_BITMAP_SIZE;i++)nova_pmm.bitmap[i]=0;
    nova_pmm.phys_base=base;
    nova_pmm.hhdm_offset=(uintptr_t)offset;
    nova_pmm.pages=pages;
    nova_pmm.in_use=0;
    nova_pmm.initialized=true;
    return true;
}
static void *nova_page_alloc(void){
    if(!nova_pmm_metadata_valid())return NULL;
    for(uint32_t i=0;i<nova_pmm.pages;i++){
        uint8_t mask=(uint8_t)(1u<<(i&7));
        if(nova_pmm.bitmap[i>>3]&mask)continue;
        nova_pmm.bitmap[i>>3]|=mask;
        nova_pmm.in_use++;
        uintptr_t mapped=(uintptr_t)(nova_pmm.phys_base+(uint64_t)i*NOVA_PAGE_SIZE)+nova_pmm.hhdm_offset;
        uint8_t *page=(uint8_t*)mapped;
        for(size_t b=0;b<NOVA_PAGE_SIZE;b++)page[b]=0;
        return page;
    }
    return NULL;
}
static bool nova_page_free(void *ptr){
    if(!ptr || !nova_pmm_metadata_valid())return false;
    uintptr_t mapped=(uintptr_t)ptr;
    uintptr_t beginning=(uintptr_t)nova_pmm.phys_base+nova_pmm.hhdm_offset;
    if(mapped<beginning)return false;
    uintptr_t delta=mapped-beginning;
    if(delta%NOVA_PAGE_SIZE || delta/NOVA_PAGE_SIZE>=nova_pmm.pages)return false;
    uint32_t i=(uint32_t)(delta/NOVA_PAGE_SIZE);
    uint8_t mask=(uint8_t)(1u<<(i&7));
    if(!(nova_pmm.bitmap[i>>3]&mask))return false;
    /* Erase the released page before making it available to another app.
       Volatile stores prevent the compiler from optimizing the wipe out. */
    volatile uint8_t *bytes=(volatile uint8_t *)ptr;
    for(size_t b=0;b<NOVA_PAGE_SIZE;b++)bytes[b]=0;
    nova_pmm.bitmap[i>>3]=(uint8_t)(nova_pmm.bitmap[i>>3]&~mask);
    nova_pmm.in_use--;
    return true;
}
/* Convert a PMM-owned HHDM page to its verified physical address.
 * Does not accept arbitrary kernel pointers or unallocated frames. */
static bool nova_pmm_physical(const void *ptr,uint64_t *physical){
    if(!ptr||!physical||!nova_pmm_metadata_valid())return false;
    uintptr_t base=(uintptr_t)nova_pmm.phys_base+nova_pmm.hhdm_offset;
    uintptr_t p=(uintptr_t)ptr;
    if(p<base)return false;
    uintptr_t delta=p-base;
    if((delta&(NOVA_PAGE_SIZE-1u))!=0||
       delta/NOVA_PAGE_SIZE>=nova_pmm.pages)return false;
    uint32_t index=(uint32_t)(delta/NOVA_PAGE_SIZE);
    if(!(nova_pmm.bitmap[index>>3]&(uint8_t)(1u<<(index&7u))))return false;
    *physical=nova_pmm.phys_base+(uint64_t)index*NOVA_PAGE_SIZE;
    return true;
}
static uint32_t nova_free_pages(void){
    return nova_pmm_metadata_valid()?nova_pmm.pages-nova_pmm.in_use:0;
}
static bool nova_pmm_self_test(void){
    if(!nova_pmm_metadata_valid())return false;
    uint32_t old=nova_free_pages();
    uint64_t *a=(uint64_t*)nova_page_alloc();
    uint64_t *b=(uint64_t*)nova_page_alloc();
    if(!a || !b || a==b){
        if(a)nova_page_free(a);
        if(b)nova_page_free(b);
        return false;
    }
    bool zero=(*a==0 && *b==0);
    *a=0x4E4F564150414745ull;
    *b=0xC0FFEE1234567890ull;
    bool good=zero && *a==0x4E4F564150414745ull && *b==0xC0FFEE1234567890ull;
    bool freedA=nova_page_free(a);
    bool doubleFreeRejected=!nova_page_free(a);
    bool freedB=nova_page_free(b);
    return good && freedA && freedB && doubleFreeRejected && old==nova_free_pages();
}
#endif
