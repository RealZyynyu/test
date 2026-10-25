/* NOVA Native OFFLINE user address-space layout, not executable processes.
 *
 * Stages a separate x86-64 PML4 tree plus four zeroed 4-KiB stack frames.
 * A fifth page immediately below the stack is intentionally UNMAPPED,
 * providing a planned guard page. The stack is writable and non-executable.
 *
 * CRITICAL: These PML4s lack kernel mappings and CANNOT be activated with
 * CR3. There is no ring 3, TSS, syscalls, interrupt return or stack switching.
 * This code only prepares and inspects data structures.
 *
 * Preparation failures attempt rollback. If a trusted release callback
 * fails, remaining stack/table tickets stay in a retry-only descriptor.
 * ELF image frames remain owned by process_image.h and are never freed here.
 */
#ifndef NOVA_USER_SPACE_H
#define NOVA_USER_SPACE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "paging.h"

#define NOVA_USER_STACK_PAGES 4u
#define NOVA_USER_STACK_TOP (NOVA_ELF64_USER_MAX - UINT64_C(0x10000))
#define NOVA_USER_STACK_GUARD (NOVA_USER_STACK_TOP - \
    (NOVA_USER_STACK_PAGES+1u)*NOVA_PT_PAGE_SIZE)

struct nova_user_space {
    struct nova_page_table page_tables;
    void *stack_frames[NOVA_USER_STACK_PAGES];
    uint64_t stack_physical[NOVA_USER_STACK_PAGES];
    uint64_t stack_generation[NOVA_USER_STACK_PAGES];
    uint32_t stack_count;
    bool stack_guarded; /* generation-checked stack ownership */
    uint64_t stack_top;
    uint64_t initial_rsp;
    uint64_t guard_base;
    bool prepared;
    bool teardown_started; /* failed releases retain tickets for retry */
};
/* Role-specific stack callbacks are optional for synthetic host allocators,
 * but the native kernel supplies all three to keep stack/table/image frames
 * in distinct PMM roles. Never accept a partially configured override. */
static inline nova_pt_alloc_fn nova_user_stack_alloc(const struct nova_pt_ops *ops){
    return ops->stack_allocate?ops->stack_allocate:ops->allocate;
}
static inline nova_pt_free_fn nova_user_stack_release(const struct nova_pt_ops *ops){
    return ops->stack_release?ops->stack_release:ops->release;
}
static inline nova_pt_phys_fn nova_user_stack_physical(const struct nova_pt_ops *ops){
    return ops->stack_physical_address?
           ops->stack_physical_address:ops->physical_address;
}
static inline bool nova_user_stack_range_valid(const struct nova_process_image *image){
    if(!image||!image->staged||!image->page_count||
       NOVA_USER_STACK_GUARD<NOVA_ELF64_USER_MIN ||
       NOVA_USER_STACK_TOP>NOVA_ELF64_USER_MAX)return false;
    for(uint32_t i=0;i<image->page_count;i++){
        uint64_t va=image->pages[i].virtual_page;
        if(va>=NOVA_USER_STACK_GUARD && va<NOVA_USER_STACK_TOP)
            return false;
    }
    return true;
}
/* Verify both ownership sets BEFORE the first release. On any
 * inconsistent count, pointer or physical alias, free nothing: a bounded
 * memory leak is safer than corrupting someone else's process image.
 * This check relies on a trustworthy physical-address callback.
 */
static inline bool nova_user_space_frames_releasable(
    const struct nova_user_space *space,const struct nova_pt_ops *ops){
    if(!space||!nova_pt_tables_releasable(&space->page_tables,ops)||
       space->stack_count>NOVA_USER_STACK_PAGES)return false;
    const uint64_t owner=space->page_tables.owner;
    if(owner && (!space->stack_guarded||
       !ops->stack_owned_ticket_allocate||
       !ops->stack_owned_ticket_physical_address||
       !ops->stack_owned_ticket_release))return false;
    if(!owner && space->stack_guarded &&
       (!ops->stack_ticket_allocate||!ops->stack_ticket_physical_address||
        !ops->stack_ticket_release))return false;
    const struct nova_page_table *tree=&space->page_tables;
    for(uint32_t i=0;i<space->stack_count;i++){
        void *frame=space->stack_frames[i];
        uint64_t phys=0;
        if(!frame||((uintptr_t)frame&(NOVA_PT_PAGE_SIZE-1u))!=0||
           !nova_pt_valid_phys(space->stack_physical[i])||
           (owner ?
               (!space->stack_generation[i] ||
                !ops->stack_owned_ticket_physical_address(ops->context,owner,
                    frame,space->stack_generation[i],&phys)) :
             space->stack_guarded ?
               (!space->stack_generation[i] ||
                !ops->stack_ticket_physical_address(ops->context,frame,
                    space->stack_generation[i],&phys)) :
               (space->stack_generation[i]!=0 ||
                !nova_user_stack_physical(ops)(ops->context,frame,&phys))) ||
           phys!=space->stack_physical[i])return false;
        for(uint32_t j=0;j<i;j++)
            if(frame==space->stack_frames[j]||
               phys==space->stack_physical[j])return false;
        for(uint16_t j=0;j<tree->protected_count;j++)
            if(frame==tree->protected_frames[j]||
               phys==tree->protected_physical[j])return false;
        for(uint16_t j=0;j<tree->tables_used;j++)
            if(frame==tree->tables[j]||
               phys==tree->table_physical[j])return false;
    }
    return true;
}
static inline bool nova_user_space_destroy(struct nova_user_space *space,
                                            const struct nova_pt_ops *ops){
    if(!space||!ops)return false;
    if(!space->teardown_started){
        if(!nova_user_space_frames_releasable(space,ops))return false;
        space->teardown_started=true;
        space->prepared=false;
        /* No query or mapping may use a tree once any leaf release starts. */
        space->page_tables.teardown_started=true;
        space->page_tables.prepared=false;
    }else if(!nova_user_space_frames_releasable(space,ops))return false;
    const uint64_t owner=space->page_tables.owner;
    while(space->stack_count){
        uint32_t i=space->stack_count-1u;
        bool released=true;
        if(owner)
            released=ops->stack_owned_ticket_release(ops->context,owner,
                space->stack_frames[i],space->stack_generation[i]);
        else if(space->stack_guarded)
            released=ops->stack_ticket_release(ops->context,
                space->stack_frames[i],space->stack_generation[i]);
        else
            nova_user_stack_release(ops)(ops->context,space->stack_frames[i]);
        if(!released)return false;
        space->stack_frames[i]=NULL;
        space->stack_physical[i]=0;
        space->stack_generation[i]=0;
        space->stack_count--;
    }
    if(!nova_pt_destroy(&space->page_tables,ops))return false;
    *space=(struct nova_user_space){0};
    return true;
}
static inline enum nova_pt_result nova_user_space_prepare(
    struct nova_user_space *space,const struct nova_pt_ops *ops,
    const struct nova_process_image *image){
    if(!space||!ops||!ops->allocate||!ops->release||
       !ops->physical_address||!nova_user_stack_range_valid(image)||
       space->prepared||space->teardown_started||space->stack_count||
       space->page_tables.tables_used||space->page_tables.teardown_started)
        return NOVA_PT_INVALID;
    if((ops->stack_allocate||ops->stack_release||
        ops->stack_physical_address) &&
       (!ops->stack_allocate||!ops->stack_release||
        !ops->stack_physical_address))return NOVA_PT_INVALID;
    const bool guarded=ops->stack_ticket_allocate||
                       ops->stack_ticket_physical_address||
                       ops->stack_ticket_release;
    if(image->owner){
        if(!ops->stack_owned_ticket_allocate||
           !ops->stack_owned_ticket_physical_address||
           !ops->stack_owned_ticket_release)return NOVA_PT_INVALID;
    }else if(guarded && (!ops->stack_ticket_allocate||
                          !ops->stack_ticket_physical_address||
                          !ops->stack_ticket_release))return NOVA_PT_INVALID;
    *space=(struct nova_user_space){0};
    space->stack_guarded=guarded || image->owner!=0;
    enum nova_pt_result result=nova_pt_prepare(&space->page_tables,ops,image);
    if(result!=NOVA_PT_OK)return result;
    space->guard_base=NOVA_USER_STACK_GUARD;
    space->stack_top=NOVA_USER_STACK_TOP;
    /* 16-byte aligned stack pointer; initial stack is all zero bytes.
     * Runtime arguments / ABI bootstrap have NOT been implemented. */
    space->initial_rsp=NOVA_USER_STACK_TOP-16u;
    for(uint32_t i=0;i<NOVA_USER_STACK_PAGES;i++){
        void *frame=NULL;
        uint64_t generation=0;
        if(image->owner){
            if(!ops->stack_owned_ticket_allocate(ops->context,image->owner,
                                                &frame,&generation)){
                result=NOVA_PT_OUT_OF_MEMORY;goto rollback;
            }
        }else if(guarded){
            if(!ops->stack_ticket_allocate(ops->context,&frame,&generation)){
                result=NOVA_PT_OUT_OF_MEMORY;goto rollback;
            }
        }else{
            frame=nova_user_stack_alloc(ops)(ops->context);
            if(!frame){result=NOVA_PT_OUT_OF_MEMORY;goto rollback;}
        }
        /* Invalid tickets must not authorize zeroing or cleanup. */
        if(space->stack_guarded && (!frame||!generation)){
            result=NOVA_PT_BAD_FRAME;goto rollback;
        }
        /* Do NOT register or free a suspect frame before checking it.
         * A broken allocator may return an image frame, an existing table,
         * or a previously accepted stack page. Releasing such a frame on
         * rollback would double-free someone else's allocation. */
        for(uint16_t j=0;j<space->page_tables.protected_count;j++)
            if(space->page_tables.protected_frames[j]==frame){
                result=NOVA_PT_BAD_FRAME;goto rollback;
            }
        for(uint16_t j=0;j<space->page_tables.tables_used;j++)
            if(space->page_tables.tables[j]==frame){
                result=NOVA_PT_BAD_FRAME;goto rollback;
            }
        for(uint32_t j=0;j<space->stack_count;j++)
            if(space->stack_frames[j]==frame){
                result=NOVA_PT_BAD_FRAME;goto rollback;
            }
        uint64_t phys=0;
        if(!(image->owner ?
              ops->stack_owned_ticket_physical_address(ops->context,image->owner,
                                                       frame,generation,&phys) :
              guarded ?
              ops->stack_ticket_physical_address(ops->context,frame,
                                                   generation,&phys) :
              nova_user_stack_physical(ops)(ops->context,frame,&phys)) ||
           !nova_pt_valid_phys(phys) ||
           ((uintptr_t)frame&(NOVA_PT_PAGE_SIZE-1u))!=0){
            /* Unknown ownership: do not free an unverified frame. */
            result=NOVA_PT_BAD_FRAME;goto rollback;
        }
        for(uint16_t j=0;j<space->page_tables.protected_count;j++)
            if(space->page_tables.protected_physical[j]==phys){
                result=NOVA_PT_BAD_FRAME;goto rollback;
            }
        for(uint16_t j=0;j<space->page_tables.tables_used;j++)
            if(space->page_tables.table_physical[j]==phys){
                result=NOVA_PT_BAD_FRAME;goto rollback;
            }
        for(uint32_t j=0;j<space->stack_count;j++)
            if(space->stack_physical[j]==phys){
                result=NOVA_PT_BAD_FRAME;goto rollback;
            }
        /* Only a validated, non-aliased frame becomes our responsibility.
         * The cached physical identity avoids re-querying mutable callbacks. */
        space->stack_frames[space->stack_count]=frame;
        space->stack_physical[space->stack_count]=phys;
        space->stack_generation[space->stack_count]=generation;
        space->stack_count++;
        uint8_t *bytes=(uint8_t*)frame;
        for(uint32_t off=0;off<NOVA_PT_PAGE_SIZE;off++)bytes[off]=0;
        uint64_t va=NOVA_USER_STACK_TOP-
                 (uint64_t)(NOVA_USER_STACK_PAGES-i)*NOVA_PT_PAGE_SIZE;
        /* Mapping may allocate a new PT. Reserve the current stack frame
         * first so a faulty allocator cannot hand it back as a table. */
        space->page_tables.pending_leaf_frame=frame;
        space->page_tables.pending_leaf_physical=phys;
        result=nova_pt_map_one(&space->page_tables,ops,va,phys,6u); /* RW,NX */
        space->page_tables.pending_leaf_frame=NULL;
        space->page_tables.pending_leaf_physical=0;
        if(result!=NOVA_PT_OK)goto rollback;
    }
    /* One intentionally unmapped page, immediately below the RW stack. */
    space->prepared=true;
    return NOVA_PT_OK;

rollback:
    nova_user_space_destroy(space,ops);
    return result;
}
/* Offline stack-layout integrity check: reject forged guard PTEs and
 * verify ALL four stack leaves against the allocator's recorded frames.
 * A mere failed lookup cannot prove that a guard slot is physically empty.
 */
static inline bool nova_user_stack_guard_is_unmapped(
                            const struct nova_user_space *space){
    if(!space||!space->prepared||
       space->stack_count!=NOVA_USER_STACK_PAGES||
       space->guard_base!=NOVA_USER_STACK_GUARD||
       space->stack_top!=NOVA_USER_STACK_TOP||
       !nova_pt_slot_empty(&space->page_tables,space->guard_base))
        return false;
    const uint64_t expected=NOVA_PT_P|NOVA_PT_RW|NOVA_PT_US|NOVA_PT_NX;
    for(uint32_t i=0;i<NOVA_USER_STACK_PAGES;i++){
        uint64_t va=NOVA_USER_STACK_TOP-
                    (uint64_t)(NOVA_USER_STACK_PAGES-i)*NOVA_PT_PAGE_SIZE;
        uint64_t phys=0,flags=0;
        if(!space->stack_frames[i] ||
           !nova_pt_valid_phys(space->stack_physical[i]) ||
           !nova_pt_lookup(&space->page_tables,va,&phys,&flags) ||
           phys!=space->stack_physical[i] || flags!=expected)
            return false;
    }
    return true;
}

/* PDROP preflight: validate every owned frame and mapping before revoking
 * permissions or releasing anything. This is OFFLINE only, not ring 3. */
static inline bool nova_user_space_is_empty(const struct nova_user_space *space){
    if(!space||space->prepared||space->teardown_started||space->stack_count||
       space->stack_guarded||space->stack_top||space->initial_rsp||space->guard_base)
        return false;
    const struct nova_page_table *tree=&space->page_tables;
    if(tree->prepared||tree->teardown_started||tree->root_physical||
       tree->tables_used||tree->mapped_pages||tree->protected_count||
       tree->pending_leaf_frame||tree->pending_leaf_physical||tree->owner||
       tree->tables_guarded)return false;
    for(uint32_t i=0;i<NOVA_USER_STACK_PAGES;i++)
        if(space->stack_frames[i]||space->stack_physical[i]||
           space->stack_generation[i])return false;
    for(uint16_t i=0;i<NOVA_PT_MAX_TABLES;i++)
        if(tree->tables[i]||tree->table_physical[i]||tree->table_generation[i])
            return false;
    for(uint16_t i=0;i<NOVA_PROCESS_MAX_PAGES;i++)
        if(tree->protected_frames[i]||tree->protected_physical[i])return false;
    for(uint16_t i=0;i<NOVA_PT_MAX_MAPPED_PAGES;i++)
        if(tree->mapped_physical[i]||tree->mapped_virtual[i]||tree->mapped_entry[i])
            return false;
    return true;
}
static inline bool nova_offline_drop_preflight(
    const struct nova_process_image *image,
    const struct nova_process_table *processes,
    const struct nova_user_space *space,const struct nova_pt_ops *ops,
    nova_process_phys_fn physical,
    const struct nova_process_guard_ops *guard,void *context){
    if(!image||!image->staged||!image->guarded||!image->owner||
       !processes||!space||image->page_count==0||
       image->page_count>NOVA_PROCESS_MAX_PAGES||
       !nova_process_frames_releasable_guarded(
           image,processes,physical,context,guard))return false;
    /* An image may be staged before address-space preparation, or image-frame
     * release may be retried after its address space was fully destroyed. */
    if(nova_user_space_is_empty(space))return true;
    if(!space->prepared||space->teardown_started||
       !space->page_tables.prepared||space->page_tables.teardown_started||
       space->page_tables.owner!=image->owner||
       space->page_tables.protected_count!=image->page_count||
       space->page_tables.mapped_pages!=
           (uint32_t)image->page_count+NOVA_USER_STACK_PAGES||
       !nova_user_space_frames_releasable(space,ops)||
       !nova_user_stack_guard_is_unmapped(space))return false;
    for(uint16_t i=0;i<image->page_count;i++){
        const struct nova_process_page *p=&image->pages[i];
        uint64_t phys=0,rights=0;
        const uint64_t expected=NOVA_PT_P|NOVA_PT_US|
            ((p->flags&2u)?NOVA_PT_RW:0u)|
            ((p->flags&1u)?0u:NOVA_PT_NX);
        if(space->page_tables.protected_frames[i]!=p->frame||
           space->page_tables.protected_physical[i]!=p->physical_page||
           !nova_pt_lookup(&space->page_tables,p->virtual_page,&phys,&rights)||
           phys!=p->physical_page||rights!=expected)return false;
    }
    return true;
}
#endif
