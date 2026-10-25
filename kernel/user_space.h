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
 * Every failure rolls back page-table and stack allocations. ELF image
 * frames remain owned by process_image.h and are never freed here.
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
    uint32_t stack_count;
    uint64_t stack_top;
    uint64_t initial_rsp;
    uint64_t guard_base;
    bool prepared;
};
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
static inline void nova_user_space_destroy(struct nova_user_space *space,
                                            const struct nova_pt_ops *ops){
    if(!space)return;
    /* Remove page-table references before releasing stack frames. */
    nova_pt_destroy(&space->page_tables,ops);
    if(ops && ops->release){
        for(uint32_t i=0;i<space->stack_count;i++)
            if(space->stack_frames[i])
                ops->release(ops->context,space->stack_frames[i]);
    }
    *space=(struct nova_user_space){0};
}
static inline enum nova_pt_result nova_user_space_prepare(
    struct nova_user_space *space,const struct nova_pt_ops *ops,
    const struct nova_process_image *image){
    if(!space||!ops||!ops->allocate||!ops->release||
       !ops->physical_address||!nova_user_stack_range_valid(image)||
       space->prepared||space->stack_count||space->page_tables.tables_used)
        return NOVA_PT_INVALID;
    *space=(struct nova_user_space){0};
    enum nova_pt_result result=nova_pt_prepare(&space->page_tables,ops,image);
    if(result!=NOVA_PT_OK)return result;
    space->guard_base=NOVA_USER_STACK_GUARD;
    space->stack_top=NOVA_USER_STACK_TOP;
    /* 16-byte aligned stack pointer; initial stack is all zero bytes.
     * Runtime arguments / ABI bootstrap have NOT been implemented. */
    space->initial_rsp=NOVA_USER_STACK_TOP-16u;
    for(uint32_t i=0;i<NOVA_USER_STACK_PAGES;i++){
        void *frame=ops->allocate(ops->context);
        if(!frame){result=NOVA_PT_OUT_OF_MEMORY;goto rollback;}
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
        if(!ops->physical_address(ops->context,frame,&phys) ||
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
static inline bool nova_user_stack_guard_is_unmapped(
                            const struct nova_user_space *space){
    if(!space||!space->prepared)return false;
    uint64_t phys=0,flags=0;
    if(nova_pt_lookup(&space->page_tables,space->guard_base,&phys,&flags))
        return false;
    if(!nova_pt_lookup(&space->page_tables,
                space->stack_top-NOVA_PT_PAGE_SIZE,&phys,&flags))
        return false;
    return (flags&(NOVA_PT_P|NOVA_PT_RW|NOVA_PT_US|NOVA_PT_NX))==
           (NOVA_PT_P|NOVA_PT_RW|NOVA_PT_US|NOVA_PT_NX);
}
#endif
