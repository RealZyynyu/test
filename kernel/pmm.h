/* NOVA physical page allocator (developer prototype).
 * Uses only Limine USABLE regions, maps through the Limine HHDM, and
 * manages a bounded 8 MiB pool. No virtual address spaces or isolation.
 * Never touches disk. Freestanding, single-core, no interrupt concurrency.
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
static bool nova_pmm_init(volatile struct limine_memmap_response *mm,
                          volatile struct limine_hhdm_response *hhdm){
    if(!mm || !hhdm)return false;
    uint64_t base=0;uint32_t pages=0;
    for(uint64_t i=0;i<mm->entry_count;i++){
        struct limine_memmap_entry *entry=mm->entries[i];
        if(!entry || entry->type!=LIMINE_MEMMAP_USABLE)continue;
        if(entry->base>UINT64_MAX-NOVA_PAGE_SIZE)continue;
        uint64_t first=nova_align_up(entry->base,NOVA_PAGE_SIZE);
        if(first<entry->base || first>=entry->base+entry->length)continue;
        uint64_t avail=(entry->base+entry->length-first)/NOVA_PAGE_SIZE;
        if(avail>pages){
            pages=avail>NOVA_PAGE_POOL_CAP?NOVA_PAGE_POOL_CAP:(uint32_t)avail;
            base=first;
            if(pages==NOVA_PAGE_POOL_CAP)break;
        }
    }
    if(pages<8)return false;
    for(size_t i=0;i<NOVA_BITMAP_SIZE;i++)nova_pmm.bitmap[i]=0;
    nova_pmm.phys_base=base;
    nova_pmm.hhdm_offset=(uintptr_t)hhdm->offset;
    nova_pmm.pages=pages;
    nova_pmm.in_use=0;
    nova_pmm.initialized=true;
    return true;
}
static void *nova_page_alloc(void){
    if(!nova_pmm.initialized)return NULL;
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
    if(!nova_pmm.initialized || !ptr)return false;
    uintptr_t mapped=(uintptr_t)ptr;
    uintptr_t beginning=(uintptr_t)nova_pmm.phys_base+nova_pmm.hhdm_offset;
    if(mapped<beginning)return false;
    uintptr_t delta=mapped-beginning;
    if(delta%NOVA_PAGE_SIZE || delta/NOVA_PAGE_SIZE>=nova_pmm.pages)return false;
    uint32_t i=(uint32_t)(delta/NOVA_PAGE_SIZE);
    uint8_t mask=(uint8_t)(1u<<(i&7));
    if(!(nova_pmm.bitmap[i>>3]&mask))return false;
    nova_pmm.bitmap[i>>3]=(uint8_t)(nova_pmm.bitmap[i>>3]&~mask);
    nova_pmm.in_use--;
    return true;
}
/* Convert a PMM-owned HHDM page to its verified physical address.
 * Does not accept arbitrary kernel pointers or unallocated frames. */
static bool nova_pmm_physical(const void *ptr,uint64_t *physical){
    if(!nova_pmm.initialized||!ptr||!physical)return false;
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
    return nova_pmm.initialized?nova_pmm.pages-nova_pmm.in_use:0;
}
static bool nova_pmm_self_test(void){
    if(!nova_pmm.initialized)return false;
    uint32_t old=nova_free_pages();
    uint64_t *a=(uint64_t*)nova_page_alloc();
    uint64_t *b=(uint64_t*)nova_page_alloc();
    if(!a || !b || a==b){
        if(a)nova_page_free(a);if(b)nova_page_free(b);
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
