/* NOVA x86-64 FOUR-LEVEL PAGE TABLE CONSTRUCTOR - OFFLINE ONLY.
 *
 * Converts staged ELF64 pages into a new, private PML4/PDPT/PD/PT tree.
 * It NEVER loads CR3, enables paging, changes CPU privilege level,
 * creates a kernel mapping, or executes untrusted bytes.
 *
 * Intended PTE protections: USER; WRITABLE only for PF_W;
 * NX for non-PF_X pages. These bits are NOT hardware enforced until a
 * future, independently audited kernel implements CR3, NXE, TSS and ring 3.
 *
 * No identity or kernel-half mapping is added; this tree MUST NOT be
 * activated as a CPU address space. The current Limine tables stay in use.
 *
 * The entire prepared tree is bounded and owned by its descriptor.
 * Failure rolls back allocated table frames, never image-data frames.
 */
#ifndef NOVA_PAGING_H
#define NOVA_PAGING_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "process_image.h"

#define NOVA_PT_PAGE_SIZE 4096u
#define NOVA_PT_MAX_TABLES 100u
/* Staged ELF images use <=32 leaves, with 4 additional offline stack leaves.
 * Bound manual mapping as well; never allow writable aliases of code pages. */
#define NOVA_PT_MAX_MAPPED_PAGES 64u
#define NOVA_PT_P UINT64_C(0x001)
#define NOVA_PT_RW UINT64_C(0x002)
#define NOVA_PT_US UINT64_C(0x004)
#define NOVA_PT_NX (UINT64_C(1)<<63)
#define NOVA_PT_ADDR UINT64_C(0x000FFFFFFFFFF000)

typedef void *(*nova_pt_alloc_fn)(void *);
typedef void (*nova_pt_free_fn)(void *,void *);
typedef bool (*nova_pt_phys_fn)(void *,const void *,uint64_t *);

struct nova_pt_ops {
    void *context;
    nova_pt_alloc_fn allocate;
    nova_pt_free_fn release;
    nova_pt_phys_fn physical_address;
};
enum nova_pt_result {
    NOVA_PT_OK,
    NOVA_PT_INVALID,
    NOVA_PT_RESOURCE_LIMIT,
    NOVA_PT_OUT_OF_MEMORY,
    NOVA_PT_BAD_FRAME,
    NOVA_PT_CONFLICT
};
struct nova_page_table {
    uint64_t root_physical; /* candidate CR3 value; DO NOT ACTIVATE */
    uint16_t tables_used;
    uint16_t mapped_pages;
    /* Reserve all staged-image frames before creating any table: even an
     * untrustworthy allocator must never zero an ELF data/code frame. */
    uint16_t protected_count;
    void *protected_frames[NOVA_PROCESS_MAX_PAGES];
    uint64_t protected_physical[NOVA_PROCESS_MAX_PAGES];
    /* Reject aliases between leaf PTEs, including RX/RW dual mappings. */
    uint64_t mapped_physical[NOVA_PT_MAX_MAPPED_PAGES];
    void *tables[NOVA_PT_MAX_TABLES];
    uint64_t table_physical[NOVA_PT_MAX_TABLES];
    bool prepared;
};
static inline const char *nova_pt_status_name(enum nova_pt_result r){
    switch(r){
        case NOVA_PT_OK:return "PAGE TABLES PREPARED (OFFLINE ONLY)";
        case NOVA_PT_INVALID:return "INVALID PAGE MAPPING";
        case NOVA_PT_RESOURCE_LIMIT:return "PAGE TABLE LIMIT REACHED";
        case NOVA_PT_OUT_OF_MEMORY:return "NOT ENOUGH MEMORY FOR TABLES";
        case NOVA_PT_BAD_FRAME:return "INVALID PHYSICAL PAGE FRAME";
        case NOVA_PT_CONFLICT:return "DUPLICATE VIRTUAL PAGE";
        default:return "UNKNOWN PAGE TABLE ERROR";
    }
}
static inline bool nova_pt_valid_phys(uint64_t p){
    return p!=0 && (p&UINT64_C(0xFFF))==0 && (p&~NOVA_PT_ADDR)==0;
}
static inline bool nova_pt_valid_user(uint64_t va){
    return (va&4095u)==0 &&
           va>=NOVA_ELF64_USER_MIN && va<NOVA_ELF64_USER_MAX;
}
static inline uint64_t *nova_pt_find(const struct nova_page_table *tree,
                                     uint64_t phys){
    for(uint16_t i=0;i<tree->tables_used;i++){
        if(tree->table_physical[i]==phys)return (uint64_t *)tree->tables[i];
    }
    return NULL;
}
static inline enum nova_pt_result nova_pt_new_table(
    struct nova_page_table *tree,const struct nova_pt_ops *ops,uint64_t **result,
    uint64_t *physical){
    if(tree->tables_used>=NOVA_PT_MAX_TABLES)return NOVA_PT_RESOURCE_LIMIT;
    void *page=ops->allocate(ops->context);
    if(!page)return NOVA_PT_OUT_OF_MEMORY;
    uint64_t phys=0;
    if(!ops->physical_address(ops->context,page,&phys)||
       !nova_pt_valid_phys(phys)||((uintptr_t)page&7u)!=0){
        ops->release(ops->context,page);
        return NOVA_PT_BAD_FRAME;
    }
    for(uint16_t i=0;i<tree->tables_used;i++){
        if(tree->table_physical[i]==phys || tree->tables[i]==page){
            /* A broken allocator must not return an already-owned frame.
             * Do not double-free the existing frame. */
            return NOVA_PT_BAD_FRAME;
        }
    }
    for(uint16_t i=0;i<tree->protected_count;i++){
        if(tree->protected_physical[i]==phys ||
           tree->protected_frames[i]==page){
            /* Ownership belongs to the staged image, not the page tables.
             * Never zero or release this possibly aliased frame. */
            return NOVA_PT_BAD_FRAME;
        }
    }
    for(uint16_t i=0;i<tree->mapped_pages;i++){
        if(tree->mapped_physical[i]==phys){
            /* A new table may not overwrite an existing leaf frame. */
            return NOVA_PT_BAD_FRAME;
        }
    }
    uint64_t *entries=(uint64_t *)page;
    for(size_t i=0;i<512;i++)entries[i]=0;
    uint16_t n=tree->tables_used++;
    tree->tables[n]=page;
    tree->table_physical[n]=phys;
    *result=entries;*physical=phys;
    return NOVA_PT_OK;
}
static inline void nova_pt_destroy(struct nova_page_table *tree,
                                    const struct nova_pt_ops *ops){
    if(!tree)return;
    if(ops && ops->release){
        for(uint16_t i=0;i<tree->tables_used;i++)
            if(tree->tables[i])ops->release(ops->context,tree->tables[i]);
    }
    *tree=(struct nova_page_table){0};
}
static inline enum nova_pt_result nova_pt_map_one(
    struct nova_page_table *tree,const struct nova_pt_ops *ops,
    uint64_t va,uint64_t physical,uint32_t flags){
    if(!tree||!tree->root_physical||!nova_pt_valid_user(va)||
       !nova_pt_valid_phys(physical) || (flags&~7u) ||
       ((flags&3u)==3u))return NOVA_PT_INVALID;
    if(tree->mapped_pages>=NOVA_PT_MAX_MAPPED_PAGES)
        return NOVA_PT_RESOURCE_LIMIT;
    /* Same physical frame must not have a writable and executable alias. */
    for(uint16_t i=0;i<tree->mapped_pages;i++)
        if(tree->mapped_physical[i]==physical)return NOVA_PT_CONFLICT;
    for(uint16_t i=0;i<tree->tables_used;i++)
        if(tree->table_physical[i]==physical)return NOVA_PT_BAD_FRAME;
    const uint16_t indices[4]={
        (uint16_t)((va>>39)&511u),(uint16_t)((va>>30)&511u),
        (uint16_t)((va>>21)&511u),(uint16_t)((va>>12)&511u)
    };
    if(indices[0]>=256)return NOVA_PT_INVALID;
    uint64_t *entries=nova_pt_find(tree,tree->root_physical);
    if(!entries)return NOVA_PT_BAD_FRAME;
    for(unsigned level=0;level<3;level++){
        uint64_t entry=entries[indices[level]];
        if(entry&NOVA_PT_P){
            if((entry&(NOVA_PT_US|NOVA_PT_RW))!=(NOVA_PT_US|NOVA_PT_RW) ||
               (entry&NOVA_PT_NX))return NOVA_PT_CONFLICT;
            entries=nova_pt_find(tree,entry&NOVA_PT_ADDR);
            if(!entries)return NOVA_PT_BAD_FRAME;
        }else{
            uint64_t *child=NULL,phys=0;
            enum nova_pt_result result=nova_pt_new_table(tree,ops,&child,&phys);
            if(result!=NOVA_PT_OK)return result;
            entries[indices[level]]=phys|NOVA_PT_P|NOVA_PT_RW|NOVA_PT_US;
            entries=child;
        }
    }
    if(entries[indices[3]]&NOVA_PT_P)return NOVA_PT_CONFLICT;
    uint64_t permissions=NOVA_PT_P|NOVA_PT_US;
    if(flags&2u)permissions|=NOVA_PT_RW;
    if(!(flags&1u))permissions|=NOVA_PT_NX;
    entries[indices[3]]=physical|permissions;
    tree->mapped_physical[tree->mapped_pages++]=physical;
    return NOVA_PT_OK;
}
static inline enum nova_pt_result nova_pt_prepare(
    struct nova_page_table *tree,const struct nova_pt_ops *ops,
    const struct nova_process_image *image){
    if(!tree||!ops||!ops->allocate||!ops->release||
       !ops->physical_address||!image||!image->staged||!image->page_count ||
       image->page_count>NOVA_PROCESS_MAX_PAGES ||
       tree->tables_used||tree->prepared)return NOVA_PT_INVALID;
    *tree=(struct nova_page_table){0};
    /* Preflight every leaf before ANY table allocation can overwrite it.
     * Physical aliases are rejected even when pointers differ. This also
     * protects against a buggy allocator handing us a staged-image frame.
     * Do not release suspect frames: ownership cannot be established. */
    for(uint16_t i=0;i<image->page_count;i++){
        const struct nova_process_page *p=&image->pages[i];
        uint64_t phys=0;
        if(!p->frame || !ops->physical_address(ops->context,p->frame,&phys) ||
           !nova_pt_valid_phys(phys))return NOVA_PT_BAD_FRAME;
        for(uint16_t j=0;j<tree->protected_count;j++){
            if(tree->protected_frames[j]==p->frame ||
               tree->protected_physical[j]==phys){
                *tree=(struct nova_page_table){0};
                return NOVA_PT_BAD_FRAME;
            }
        }
        tree->protected_frames[tree->protected_count]=p->frame;
        tree->protected_physical[tree->protected_count++]=phys;
    }
    uint64_t *root=NULL,root_phys=0;
    enum nova_pt_result status=nova_pt_new_table(tree,ops,&root,&root_phys);
    if(status!=NOVA_PT_OK){nova_pt_destroy(tree,ops);return status;}
    tree->root_physical=root_phys;
    for(uint16_t i=0;i<image->page_count;i++){
        const struct nova_process_page *p=&image->pages[i];
        /* Use the already validated physical address, not a second
         * potentially inconsistent allocator callback result. */
        uint64_t phys=tree->protected_physical[i];
        status=nova_pt_map_one(tree,ops,p->virtual_page,phys,p->flags);
        if(status!=NOVA_PT_OK){nova_pt_destroy(tree,ops);return status;}
    }
    tree->prepared=true;
    return NOVA_PT_OK;
}
static inline bool nova_pt_lookup(const struct nova_page_table *tree,
                   uint64_t va,uint64_t *physical,uint64_t *attributes){
    if(!tree||!tree->prepared||!nova_pt_valid_user(va)||!physical||!attributes)
        return false;
    uint16_t indices[4]={
        (uint16_t)((va>>39)&511u),(uint16_t)((va>>30)&511u),
        (uint16_t)((va>>21)&511u),(uint16_t)((va>>12)&511u)
    };
    uint64_t *entries=nova_pt_find(tree,tree->root_physical);
    if(!entries)return false;
    for(unsigned level=0;level<3;level++){
        uint64_t entry=entries[indices[level]];
        if(!(entry&NOVA_PT_P)||!(entry&NOVA_PT_US))return false;
        entries=nova_pt_find(tree,entry&NOVA_PT_ADDR);
        if(!entries)return false;
    }
    uint64_t entry=entries[indices[3]];
    if(!(entry&NOVA_PT_P)||!(entry&NOVA_PT_US))return false;
    *physical=entry&NOVA_PT_ADDR;
    *attributes=entry&~NOVA_PT_ADDR;
    return true;
}
#endif
