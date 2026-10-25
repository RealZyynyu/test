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
/* Optional table/stack generation callbacks. A ticket must come from the
 * allocator, never be reconstructed from a stale pointer. */
typedef bool (*nova_pt_ticket_alloc_fn)(void *,void **,uint64_t *);
typedef bool (*nova_pt_ticket_phys_fn)(void *,const void *,uint64_t,uint64_t *);
typedef bool (*nova_pt_ticket_free_fn)(void *,void *,uint64_t);

/* physical_address validates staged IMAGE frames during preflight.
 * Optional role-specific callbacks let the native PMM enforce independent
 * PAGE_TABLE and STACK ownership. Host tests may omit them and use the
 * original common callbacks; partial stack overrides are rejected. */
struct nova_pt_ops {
    void *context;
    nova_pt_alloc_fn allocate;
    nova_pt_free_fn release;
    nova_pt_phys_fn physical_address;
    nova_pt_phys_fn table_physical_address;
    nova_pt_ticket_alloc_fn table_ticket_allocate;
    nova_pt_ticket_phys_fn table_ticket_physical_address;
    nova_pt_ticket_free_fn table_ticket_release;
    nova_pt_alloc_fn stack_allocate;
    nova_pt_free_fn stack_release;
    nova_pt_phys_fn stack_physical_address;
    nova_pt_ticket_alloc_fn stack_ticket_allocate;
    nova_pt_ticket_phys_fn stack_ticket_physical_address;
    nova_pt_ticket_free_fn stack_ticket_release;
};
static inline nova_pt_phys_fn nova_pt_table_physical(const struct nova_pt_ops *ops){
    return ops->table_physical_address?
           ops->table_physical_address:ops->physical_address;
}
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
    /* Offline mapping manifest: a PTE must match the recorded VA, frame
     * and intended rights. Raw PTEs alone are not an access authority. */
    uint64_t mapped_virtual[NOVA_PT_MAX_MAPPED_PAGES];
    uint64_t mapped_entry[NOVA_PT_MAX_MAPPED_PAGES];
    /* Temporary reservation while a new stack leaf is mapped. A new page
     * table must never borrow/zero that not-yet-installed leaf frame. */
    void *pending_leaf_frame;
    uint64_t pending_leaf_physical;
    void *tables[NOVA_PT_MAX_TABLES];
    uint64_t table_physical[NOVA_PT_MAX_TABLES];
    uint64_t table_generation[NOVA_PT_MAX_TABLES];
    bool tables_guarded; /* generation-checked table frame ownership */
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
    if(!tree||tree->tables_used>NOVA_PT_MAX_TABLES)return NULL;
    for(uint16_t i=0;i<tree->tables_used;i++){
        if(tree->table_physical[i]==phys)return (uint64_t *)tree->tables[i];
    }
    return NULL;
}
static inline enum nova_pt_result nova_pt_new_table(
    struct nova_page_table *tree,const struct nova_pt_ops *ops,uint64_t **result,
    uint64_t *physical){
    if(tree->tables_used>=NOVA_PT_MAX_TABLES)return NOVA_PT_RESOURCE_LIMIT;
    void *page=NULL;
    uint64_t generation=0;
    if(tree->tables_guarded){
        if(!ops->table_ticket_allocate||!ops->table_ticket_physical_address||
           !ops->table_ticket_release)return NOVA_PT_INVALID;
        if(!ops->table_ticket_allocate(ops->context,&page,&generation))
            return NOVA_PT_OUT_OF_MEMORY;
        /* A forged/empty ticket cannot authorize writes or cleanup. */
        if(!page||!generation)return NOVA_PT_BAD_FRAME;
    }else{
        page=ops->allocate(ops->context);
        if(!page)return NOVA_PT_OUT_OF_MEMORY;
    }
    /* First check pointer identity, before invoking an unreliable physical
     * address callback or freeing anything. Borrowed frames are NOT ours. */
    for(uint16_t i=0;i<tree->tables_used;i++)
        if(tree->tables[i]==page)return NOVA_PT_BAD_FRAME;
    for(uint16_t i=0;i<tree->protected_count;i++)
        if(tree->protected_frames[i]==page)return NOVA_PT_BAD_FRAME;
    if(tree->pending_leaf_frame==page)return NOVA_PT_BAD_FRAME;
    uint64_t phys=0;
    if(!(tree->tables_guarded ?
         ops->table_ticket_physical_address(ops->context,page,generation,&phys) :
         nova_pt_table_physical(ops)(ops->context,page,&phys)) ||
       !nova_pt_valid_phys(phys)||
       ((uintptr_t)page&(NOVA_PT_PAGE_SIZE-1u))!=0){
        /* Cannot establish ownership of a suspect allocation. Releasing it
         * could double-free an image/stack frame through a faulty callback.
         * Fail closed, potentially leaking the suspect frame instead. */
        return NOVA_PT_BAD_FRAME;
    }
    for(uint16_t i=0;i<tree->tables_used;i++)
        if(tree->table_physical[i]==phys)return NOVA_PT_BAD_FRAME;
    for(uint16_t i=0;i<tree->protected_count;i++)
        if(tree->protected_physical[i]==phys)return NOVA_PT_BAD_FRAME;
    for(uint16_t i=0;i<tree->mapped_pages;i++)
        if(tree->mapped_physical[i]==phys)return NOVA_PT_BAD_FRAME;
    if(tree->pending_leaf_physical==phys)return NOVA_PT_BAD_FRAME;
    uint64_t *entries=(uint64_t *)page;
    for(size_t i=0;i<512;i++)entries[i]=0;
    uint16_t n=tree->tables_used++;
    tree->tables[n]=page;
    tree->table_physical[n]=phys;
    tree->table_generation[n]=generation;
    *result=entries;*physical=phys;
    return NOVA_PT_OK;
}
/* Offline teardown must never trust unbounded or inconsistent ownership
 * metadata. Check the COMPLETE table set before releasing even one frame:
 * a partial cleanup after an alias/damaged count could double-free a staged
 * image or a stack frame. Unknown ownership deliberately leaks memory.
 *
 * This is defensive bookkeeping, NOT an authenticated ownership proof:
 * a malicious physical_address callback or simultaneous metadata tampering
 * remains outside this prototype's trust model.
 */
static inline bool nova_pt_tables_releasable(
    const struct nova_page_table *tree,const struct nova_pt_ops *ops){
    if(!tree||!ops||!ops->release||!ops->physical_address||
       tree->tables_used>NOVA_PT_MAX_TABLES||
       tree->protected_count>NOVA_PROCESS_MAX_PAGES||
       tree->mapped_pages>NOVA_PT_MAX_MAPPED_PAGES||
       tree->pending_leaf_frame||tree->pending_leaf_physical)return false;
    if(tree->tables_guarded &&
       (!ops->table_ticket_allocate||!ops->table_ticket_physical_address||
        !ops->table_ticket_release))return false;
    if(tree->tables_used &&
       tree->root_physical!=tree->table_physical[0])return false;
    for(uint16_t i=0;i<tree->tables_used;i++){
        void *frame=tree->tables[i];
        uint64_t phys=0;
        if(!frame||((uintptr_t)frame&(NOVA_PT_PAGE_SIZE-1u))!=0||
           !nova_pt_valid_phys(tree->table_physical[i])||
           (tree->tables_guarded ?
             (!tree->table_generation[i] ||
              !ops->table_ticket_physical_address(ops->context,frame,
                   tree->table_generation[i],&phys)) :
             (tree->table_generation[i]!=0 ||
              !nova_pt_table_physical(ops)(ops->context,frame,&phys))) ||
           phys!=tree->table_physical[i])return false;
        for(uint16_t j=0;j<i;j++)
            if(frame==tree->tables[j]||
               phys==tree->table_physical[j])return false;
        for(uint16_t j=0;j<tree->protected_count;j++)
            if(frame==tree->protected_frames[j]||
               phys==tree->protected_physical[j])return false;
        for(uint16_t j=0;j<tree->mapped_pages;j++)
            if(phys==tree->mapped_physical[j])return false;
    }
    return true;
}
static inline void nova_pt_destroy(struct nova_page_table *tree,
                                    const struct nova_pt_ops *ops){
    if(!tree)return;
    if(nova_pt_tables_releasable(tree,ops)){
        for(uint16_t i=0;i<tree->tables_used;i++){
            if(tree->tables_guarded)
                (void)ops->table_ticket_release(ops->context,tree->tables[i],
                                                tree->table_generation[i]);
            else
                ops->release(ops->context,tree->tables[i]);
        }
    }
    /* Even when ownership cannot be verified, discard the suspect tree.
     * Retaining it could allow accidental reuse of compromised metadata. */
    *tree=(struct nova_page_table){0};
}
static inline enum nova_pt_result nova_pt_map_one(
    struct nova_page_table *tree,const struct nova_pt_ops *ops,
    uint64_t va,uint64_t physical,uint32_t flags){
    if(!tree||!ops||!tree->root_physical||!nova_pt_valid_user(va)||
       !nova_pt_valid_phys(physical) || (flags&~7u) ||
       ((flags&3u)==3u) || tree->tables_used>NOVA_PT_MAX_TABLES ||
       tree->mapped_pages>NOVA_PT_MAX_MAPPED_PAGES ||
       (tree->tables_guarded &&
        (!ops->table_ticket_allocate||!ops->table_ticket_physical_address||
         !ops->table_ticket_release)))return NOVA_PT_INVALID;
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
            /* Offline page tables have never been activated. No CPU A/D
             * bits should exist; reject mutated upper-level entries. */
            if(entry!=((entry&NOVA_PT_ADDR)|
                       NOVA_PT_P|NOVA_PT_US|NOVA_PT_RW) ||
               !nova_pt_valid_phys(entry&NOVA_PT_ADDR))
                return NOVA_PT_CONFLICT;
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
    uint16_t slot=tree->mapped_pages++;
    uint64_t entry=physical|permissions;
    entries[indices[3]]=entry;
    tree->mapped_physical[slot]=physical;
    tree->mapped_virtual[slot]=va;
    tree->mapped_entry[slot]=entry;
    return NOVA_PT_OK;
}
static inline enum nova_pt_result nova_pt_prepare(
    struct nova_page_table *tree,const struct nova_pt_ops *ops,
    const struct nova_process_image *image){
    if(!tree||!ops||!ops->allocate||!ops->release||
       !ops->physical_address||!image||!image->staged||!image->page_count ||
       image->page_count>NOVA_PROCESS_MAX_PAGES ||
       tree->tables_used||tree->prepared)return NOVA_PT_INVALID;
    const bool guarded=ops->table_ticket_allocate||
                       ops->table_ticket_physical_address||
                       ops->table_ticket_release;
    if(guarded && (!ops->table_ticket_allocate||
                   !ops->table_ticket_physical_address||
                   !ops->table_ticket_release))return NOVA_PT_INVALID;
    *tree=(struct nova_page_table){0};
    tree->tables_guarded=guarded;
    /* Preflight every leaf before ANY table allocation can overwrite it.
     * Physical aliases are rejected even when pointers differ. This also
     * protects against a buggy allocator handing us a staged-image frame.
     * Do not release suspect frames: ownership cannot be established. */
    for(uint16_t i=0;i<image->page_count;i++){
        const struct nova_process_page *p=&image->pages[i];
        uint64_t phys=0;
        if(!p->frame || !ops->physical_address(ops->context,p->frame,&phys) ||
           (!nova_pt_valid_phys(phys) || phys!=p->physical_page))return NOVA_PT_BAD_FRAME;
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
/* Only addresses in the immutable-intent manifest may be looked up.
 * An unexpectedly present PTE (including a forged guard mapping), changed
 * rights or altered page-table links must never authorize a user buffer.
 * This is OFFLINE validation, not a tamper-proof runtime page table.
 */
static inline bool nova_pt_lookup(const struct nova_page_table *tree,
                   uint64_t va,uint64_t *physical,uint64_t *attributes){
    if(!tree||!tree->prepared||!nova_pt_valid_user(va)||
       !physical||!attributes||
       tree->mapped_pages>NOVA_PT_MAX_MAPPED_PAGES||
       tree->tables_used>NOVA_PT_MAX_TABLES)return false;
    uint64_t expected=0;
    bool found=false;
    for(uint16_t i=0;i<tree->mapped_pages;i++){
        if(tree->mapped_virtual[i]!=va)continue;
        if(found)return false; /* duplicate manifest entries are corrupt */
        expected=tree->mapped_entry[i];
        if((expected&NOVA_PT_ADDR)!=tree->mapped_physical[i])
            return false;
        found=true;
    }
    if(!found || !nova_pt_valid_phys(expected&NOVA_PT_ADDR) ||
       (expected&~(NOVA_PT_ADDR|NOVA_PT_P|NOVA_PT_RW|
                    NOVA_PT_US|NOVA_PT_NX))!=0 ||
       (expected&(NOVA_PT_P|NOVA_PT_US))!=(NOVA_PT_P|NOVA_PT_US) ||
       ((expected&NOVA_PT_RW)!=0 && (expected&NOVA_PT_NX)==0))
        return false;
    uint16_t indices[4]={
        (uint16_t)((va>>39)&511u),(uint16_t)((va>>30)&511u),
        (uint16_t)((va>>21)&511u),(uint16_t)((va>>12)&511u)
    };
    uint64_t *entries=nova_pt_find(tree,tree->root_physical);
    if(!entries)return false;
    for(unsigned level=0;level<3;level++){
        uint64_t entry=entries[indices[level]];
        if(entry!=((entry&NOVA_PT_ADDR)|
                   NOVA_PT_P|NOVA_PT_RW|NOVA_PT_US) ||
           !nova_pt_valid_phys(entry&NOVA_PT_ADDR))return false;
        entries=nova_pt_find(tree,entry&NOVA_PT_ADDR);
        if(!entries)return false;
    }
    if(entries[indices[3]]!=expected)return false;
    *physical=expected&NOVA_PT_ADDR;
    *attributes=expected&~NOVA_PT_ADDR;
    return true;
}
/* An unmapped guard is not just an address rejected by nova_pt_lookup:
 * a corrupted, unregistered, or non-present-but-nonzero PTE must also fail.
 * Empty upper-level slots are safe; malformed links are not.
 */
static inline bool nova_pt_slot_empty(const struct nova_page_table *tree,
                                      uint64_t va){
    if(!tree||!tree->prepared||!nova_pt_valid_user(va)||
       tree->mapped_pages>NOVA_PT_MAX_MAPPED_PAGES||
       tree->tables_used>NOVA_PT_MAX_TABLES)return false;
    for(uint16_t i=0;i<tree->mapped_pages;i++)
        if(tree->mapped_virtual[i]==va)return false;
    uint16_t indices[4]={
        (uint16_t)((va>>39)&511u),(uint16_t)((va>>30)&511u),
        (uint16_t)((va>>21)&511u),(uint16_t)((va>>12)&511u)
    };
    uint64_t *entries=nova_pt_find(tree,tree->root_physical);
    if(!entries)return false;
    for(unsigned level=0;level<3;level++){
        uint64_t entry=entries[indices[level]];
        if(entry==0)return true;
        if(entry!=((entry&NOVA_PT_ADDR)|
                   NOVA_PT_P|NOVA_PT_RW|NOVA_PT_US) ||
           !nova_pt_valid_phys(entry&NOVA_PT_ADDR))return false;
        entries=nova_pt_find(tree,entry&NOVA_PT_ADDR);
        if(!entries)return false;
    }
    return entries[indices[3]]==0;
}
#endif
