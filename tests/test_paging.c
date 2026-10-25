/* Page-table host tests: CPU page-table registers are NEVER modified.
 * Simulated page-aligned physical frames exercise the real page-table bits.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/paging.h"
#include "../kernel/user_space.h"
#include "../kernel/user_buffer.h"
#define POOL 192u
#define BINARY_BYTES 16384u

struct physical_page { _Alignas(4096) uint8_t bytes[4096]; };
static struct physical_page pool[POOL];
static bool allocated[POOL];
static uint32_t active=0;
static uint32_t allocation_budget=UINT32_MAX;
static uint8_t binary[BINARY_BYTES];

static void *allocate(void *unused){
    (void)unused;
    if(allocation_budget==0)return NULL;
    for(unsigned i=0;i<POOL;i++)if(!allocated[i]){
        allocated[i]=true;active++;
        if(allocation_budget!=UINT32_MAX)allocation_budget--;
        memset(pool[i].bytes,0xE1,4096);
        return pool[i].bytes;
    }
    return NULL;
}
static void release(void *unused,void *frame){
    (void)unused;
    for(unsigned i=0;i<POOL;i++)if(frame==pool[i].bytes){
        assert(allocated[i]);
        allocated[i]=false;active--;
        return;
    }
    assert(!"invalid physical page");
}
static bool physical(void *unused,const void *frame,uint64_t *out){
    (void)unused;
    if(!out)return false;
    for(unsigned i=0;i<POOL;i++)if(frame==pool[i].bytes && allocated[i]){
        *out=UINT64_C(0x100000)+(uint64_t)i*4096;
        return true;
    }
    return false;
}
static const struct nova_pt_ops ops={
    .context=NULL,.allocate=allocate,.release=release,.physical_address=physical
};
/* Deliberately faulty allocator/callbacks: the paging builder must fail
 * closed rather than overwrite image frames or install physical aliases. */
static void *borrowed_allocate(void *frame){return frame;}
struct alias_phys_context {
    const void *aliased_frame;
    uint64_t reported_physical;
};
static bool lying_physical(void *ctx,const void *frame,uint64_t *out){
    struct alias_phys_context *alias=(struct alias_phys_context *)ctx;
    if(!physical(NULL,frame,out))return false;
    if(frame==alias->aliased_frame)*out=alias->reported_physical;
    return true;
}

static void u16(size_t at,uint16_t v){
    binary[at]=(uint8_t)v;binary[at+1]=(uint8_t)(v>>8);
}
static void u32(size_t at,uint32_t v){
    for(unsigned i=0;i<4;i++)binary[at+i]=(uint8_t)(v>>(i*8));
}
static void u64(size_t at,uint64_t v){
    for(unsigned i=0;i<8;i++)binary[at+i]=(uint8_t)(v>>(i*8));
}
static void ph(unsigned i,uint32_t flags,uint64_t offset,uint64_t va,
               uint64_t file_size,uint64_t mem_size){
    size_t base=64u+(size_t)i*56u;
    u32(base,1);u32(base+4,flags);u64(base+8,offset);
    u64(base+16,va);u64(base+32,file_size);
    u64(base+40,mem_size);u64(base+48,4096);
}
static void image_fixture(void){
    memset(binary,0,sizeof(binary));
    binary[0]=0x7f;binary[1]='E';binary[2]='L';binary[3]='F';
    binary[4]=2;binary[5]=1;binary[6]=1;
    u16(16,2);u16(18,62);u32(20,1);
    u64(24,0x401000);u64(32,64);
    u16(52,64);u16(54,56);u16(56,2);
    ph(0,5,4096,0x401000,128,4096);
    ph(1,6,8192,0x402000,128,8192);
    for(unsigned i=0;i<128;i++)binary[4096+i]=(uint8_t)(i+1);
}
static bool read_binary(void *unused,uint64_t offset,uint8_t *out,size_t n){
    (void)unused;
    if(offset>BINARY_BYTES||n>BINARY_BYTES-offset)return false;
    memcpy(out,binary+(size_t)offset,n);
    return true;
}
static uint32_t stage(struct nova_process_table *table){
    uint32_t pid=0;
    assert(nova_process_stage(table,"DISK:/TEST.ELF",read_binary,NULL,
           BINARY_BYTES,allocate,release,NULL,&pid)==NOVA_PROCESS_OK);
    return pid;
}
static void test_page_tables(void){
    image_fixture();
    struct nova_process_table processes={0};
    uint32_t first=stage(&processes);
    struct nova_process_image *img=nova_process_find(&processes,first);
    assert(img!=NULL);
    assert(active==3);
    struct nova_page_table tree={0};
    assert(nova_pt_prepare(&tree,&ops,img)==NOVA_PT_OK);
    assert(tree.prepared && tree.root_physical && tree.mapped_pages==3);
    assert(tree.tables_used==4); /* one PML4, PDPT, PD and PT */
    assert(active==7);
    uint64_t phys=0,attr=0;
    assert(nova_pt_lookup(&tree,0x401000,&phys,&attr));
    assert(phys!=tree.table_physical[0]); /* leaf != root */
    assert(phys!=(tree.root_physical));
    assert((attr&(NOVA_PT_P|NOVA_PT_US))==(NOVA_PT_P|NOVA_PT_US));
    assert(!(attr&NOVA_PT_RW) && !(attr&NOVA_PT_NX)); /* RX code */
    assert(nova_pt_lookup(&tree,0x402000,&phys,&attr));
    assert((attr&(NOVA_PT_P|NOVA_PT_RW|NOVA_PT_US|NOVA_PT_NX))==
           (NOVA_PT_P|NOVA_PT_RW|NOVA_PT_US|NOVA_PT_NX)); /* RW, NX */
    assert(nova_pt_lookup(&tree,0x403000,&phys,&attr));
    assert((attr&NOVA_PT_NX)!=0);
    assert(!nova_pt_lookup(&tree,0x404000,&phys,&attr));
    assert(!nova_pt_lookup(&tree,0xFFFF800000000000ull,&phys,&attr));
    assert(!nova_pt_lookup(&tree,0x401000,NULL,&attr));
    assert(!nova_pt_lookup(&tree,0x401000,&phys,NULL));
    assert(nova_pt_map_one(&tree,&ops,0x401000,phys,5)==NOVA_PT_CONFLICT);
    assert(nova_pt_map_one(&tree,&ops,0xFFFF800000000000ull,phys,5)==NOVA_PT_INVALID);
    assert(nova_pt_map_one(&tree,&ops,0x401123,phys,5)==NOVA_PT_INVALID);
    assert(nova_pt_map_one(&tree,&ops,0x405000,phys,7)==NOVA_PT_INVALID);
    assert(nova_pt_map_one(&tree,&ops,0x405000,3,5)==NOVA_PT_INVALID);
    assert(!strcmp(nova_pt_status_name(NOVA_PT_OK),"PAGE TABLES PREPARED (OFFLINE ONLY)"));
    assert(nova_elf_result_name(NOVA_ELF_OK)[0]!=0);
    assert(nova_pt_prepare(&tree,&ops,img)==NOVA_PT_INVALID);
    nova_pt_destroy(&tree,&ops);
    assert(active==3 && !tree.prepared && tree.tables_used==0);
    assert(nova_process_drop(&processes,first,release,NULL));
    assert(active==0);
}
static void test_isolated_roots(void){
    image_fixture();
    struct nova_process_table p={0};
    uint32_t id1=stage(&p),id2=stage(&p);
    struct nova_page_table a={0},b={0};
    assert(nova_pt_prepare(&a,&ops,nova_process_find(&p,id1))==NOVA_PT_OK);
    assert(nova_pt_prepare(&b,&ops,nova_process_find(&p,id2))==NOVA_PT_OK);
    assert(a.root_physical!=b.root_physical);
    uint64_t ap=0,bp=0,af=0,bf=0;
    assert(nova_pt_lookup(&a,0x401000,&ap,&af));
    assert(nova_pt_lookup(&b,0x401000,&bp,&bf));
    assert(ap!=bp && af==bf);
    uint64_t alloc_before=active;
    nova_pt_destroy(&a,&ops);
    assert(active==alloc_before-4u);
    assert(nova_pt_lookup(&b,0x402000,&bp,&bf));
    nova_pt_destroy(&b,&ops);
    assert(nova_process_drop(&p,id1,release,NULL));
    assert(nova_process_drop(&p,id2,release,NULL));
    assert(active==0);
}
static void test_failures(void){
    image_fixture();
    struct nova_process_table p={0};
    uint32_t id=stage(&p);
    struct nova_process_image *img=nova_process_find(&p,id);
    struct nova_page_table t={0};
    allocation_budget=2;
    assert(nova_pt_prepare(&t,&ops,img)==NOVA_PT_OUT_OF_MEMORY);
    assert(t.tables_used==0 && !t.prepared && active==3);
    allocation_budget=UINT32_MAX;

    /* A nonexistent physical address cannot enter any user PTE. */
    uint8_t *saved=img->pages[0].frame;
    img->pages[0].frame=(uint8_t*)UINTPTR_MAX;
    assert(nova_pt_prepare(&t,&ops,img)==NOVA_PT_BAD_FRAME);
    assert(!t.tables_used && active==3);
    img->pages[0].frame=saved;

    img->pages[0].flags=7; /* W+X is rejected even after ELF validation. */
    assert(nova_pt_prepare(&t,&ops,img)==NOVA_PT_INVALID);
    assert(active==3);
    img->pages[0].flags=5;

    uint64_t oldva=img->pages[1].virtual_page;
    img->pages[1].virtual_page=img->pages[0].virtual_page;
    assert(nova_pt_prepare(&t,&ops,img)==NOVA_PT_CONFLICT);
    assert(active==3);
    img->pages[1].virtual_page=oldva;

    struct nova_pt_ops invalid=ops;
    invalid.physical_address=NULL;
    assert(nova_pt_prepare(&t,&invalid,img)==NOVA_PT_INVALID);
    assert(nova_pt_prepare(NULL,&ops,img)==NOVA_PT_INVALID);
    assert(nova_pt_prepare(&t,&ops,NULL)==NOVA_PT_INVALID);
    assert(!nova_pt_lookup(&t,0x401000,NULL,NULL));
    assert(nova_process_drop(&p,id,release,NULL));
    assert(active==0);
}
static void test_physical_alias_guards(void){
    image_fixture();
    struct nova_process_table processes={0};
    uint32_t id=stage(&processes);
    struct nova_process_image *img=nova_process_find(&processes,id);
    assert(img && img->page_count==3 && active==3);
    struct nova_page_table tree={0};
    uint8_t first_bytes[32];
    memcpy(first_bytes,img->pages[0].frame,sizeof(first_bytes));

    /* The same pointer for two virtual pages would defeat W^X. */
    uint8_t *saved_second=img->pages[1].frame;
    img->pages[1].frame=img->pages[0].frame;
    assert(nova_pt_prepare(&tree,&ops,img)==NOVA_PT_BAD_FRAME);
    assert(!tree.tables_used && !tree.prepared && active==3);
    img->pages[1].frame=saved_second;

    /* Two different pointers claiming the same physical page are unsafe. */
    uint64_t first_phys=0;
    assert(physical(NULL,img->pages[0].frame,&first_phys));
    struct alias_phys_context alias={
        .aliased_frame=img->pages[1].frame,
        .reported_physical=first_phys
    };
    struct nova_pt_ops dishonest=ops;
    dishonest.context=&alias;
    dishonest.physical_address=lying_physical;
    assert(nova_pt_prepare(&tree,&dishonest,img)==NOVA_PT_BAD_FRAME);
    assert(!tree.tables_used && active==3);

    /* A broken allocator returning an image frame must never zero or free it. */
    struct nova_pt_ops borrowed=ops;
    borrowed.context=img->pages[0].frame;
    borrowed.allocate=borrowed_allocate;
    assert(nova_pt_prepare(&tree,&borrowed,img)==NOVA_PT_BAD_FRAME);
    assert(!tree.tables_used && active==3);
    assert(!memcmp(first_bytes,img->pages[0].frame,sizeof(first_bytes)));

    /* Distinct virtual addresses may not share a physical leaf, regardless
     * of whether their intended permissions are RX or RW/NX. */
    assert(nova_pt_prepare(&tree,&ops,img)==NOVA_PT_OK);
    uint64_t phys=0,flags=0;
    assert(nova_pt_lookup(&tree,0x401000,&phys,&flags));
    uint16_t leaves=tree.mapped_pages;
    uint16_t tables=tree.tables_used;
    assert(nova_pt_map_one(&tree,&ops,0x404000,phys,6u)==NOVA_PT_CONFLICT);
    assert(nova_pt_map_one(&tree,&ops,0x404000,
                           tree.root_physical,6u)==NOVA_PT_BAD_FRAME);
    assert(tree.mapped_pages==leaves && tree.tables_used==tables);
    assert(!nova_pt_lookup(&tree,0x404000,&phys,&flags));

    /* An allocator cannot use an already-mapped leaf as a new PT. */
    assert(nova_pt_map_one(&tree,&borrowed,0x800000,
                           UINT64_C(0x900000),6u)==NOVA_PT_BAD_FRAME);
    assert(tree.tables_used==tables && tree.mapped_pages==leaves);
    assert(!memcmp(first_bytes,img->pages[0].frame,sizeof(first_bytes)));

    nova_pt_destroy(&tree,&ops);
    assert(active==3);
    assert(nova_process_drop(&processes,id,release,NULL));
    assert(active==0);
}
static void test_guarded_user_stack(void){
    image_fixture();
    struct nova_process_table p={0};
    uint32_t pid=stage(&p);
    struct nova_process_image *img=nova_process_find(&p,pid);
    assert(active==3);
    struct nova_user_space space={0};
    assert(nova_user_space_prepare(&space,&ops,img)==NOVA_PT_OK);
    assert(space.prepared);
    assert(space.stack_count==NOVA_USER_STACK_PAGES);
    assert(space.initial_rsp==NOVA_USER_STACK_TOP-16);
    assert((space.initial_rsp&15u)==0);
    assert(space.stack_top==NOVA_USER_STACK_TOP);
    assert(space.guard_base==NOVA_USER_STACK_GUARD);
    assert(space.page_tables.mapped_pages==img->page_count+NOVA_USER_STACK_PAGES);
    assert(nova_user_stack_guard_is_unmapped(&space));
    /* Offline copyin/copyout pointer span checking; no user pointer derefs. */
    assert(nova_user_span_accessible(&space,0x401000,16,false));
    assert(!nova_user_span_accessible(&space,0x401000,16,true)); /* RX */
    assert(nova_user_span_accessible(&space,0x402000,4096,true)); /* RW data */
    assert(nova_user_span_accessible(&space,0x402fff,2,true)); /* crosses into RW */
    assert(nova_user_span_accessible(&space,NOVA_USER_STACK_TOP-64u,64u,true));
    assert(!nova_user_span_accessible(&space,NOVA_USER_STACK_GUARD,16,false));
    assert(!nova_user_span_accessible(&space,NOVA_USER_STACK_GUARD,16,true));
    assert(!nova_user_span_accessible(&space,NOVA_USER_STACK_GUARD+4095u,2u,true));
    assert(!nova_user_span_accessible(&space,NOVA_USER_STACK_TOP-1u,2u,true));
    assert(!nova_user_span_accessible(&space,0x400000,10,false));
    assert(!nova_user_span_accessible(&space,UINT64_MAX-2u,20,false));
    assert(!nova_user_span_accessible(&space,0x401000,0,false));
    assert(!nova_user_span_accessible(&space,0x401000,4097u,false));
    assert(!nova_user_span_accessible(NULL,0x401000,4,false));
    uint64_t phys=0,flags=0;
    assert(!nova_pt_lookup(&space.page_tables,NOVA_USER_STACK_GUARD,&phys,&flags));
    assert(!nova_pt_lookup(&space.page_tables,NOVA_USER_STACK_TOP,&phys,&flags));
    assert(nova_pt_lookup(&space.page_tables,
              NOVA_USER_STACK_TOP-NOVA_USER_STACK_PAGES*4096u,&phys,&flags));
    assert((flags&(NOVA_PT_P|NOVA_PT_RW|NOVA_PT_US|NOVA_PT_NX))==
           (NOVA_PT_P|NOVA_PT_RW|NOVA_PT_US|NOVA_PT_NX));
    for(uint32_t i=0;i<space.stack_count;i++){
        assert(space.stack_frames[i]!=NULL);
        for(unsigned b=0;b<4096;b++)
            assert(((uint8_t*)space.stack_frames[i])[b]==0);
    }
    assert(nova_pt_lookup(&space.page_tables,0x401000,&phys,&flags));
    assert(!(flags&NOVA_PT_RW)&&!(flags&NOVA_PT_NX)); /* code RX */
    assert(nova_pt_lookup(&space.page_tables,0x402000,&phys,&flags));
    assert((flags&(NOVA_PT_RW|NOVA_PT_NX))==(NOVA_PT_RW|NOVA_PT_NX));
    assert(nova_user_space_prepare(&space,&ops,img)==NOVA_PT_INVALID);
    nova_user_space_destroy(&space,&ops);
    assert(active==3&&!space.prepared);
    assert(nova_process_drop(&p,pid,release,NULL));
    assert(active==0);
}
static void test_stack_failures(void){
    image_fixture();
    struct nova_process_table p={0};
    uint32_t pid=stage(&p);
    struct nova_process_image *img=nova_process_find(&p,pid);
    struct nova_user_space space={0};
    assert(!nova_user_stack_guard_is_unmapped(&space));

    uint64_t old_va=img->pages[0].virtual_page;
    img->pages[0].virtual_page=NOVA_USER_STACK_GUARD;
    assert(nova_user_space_prepare(&space,&ops,img)==NOVA_PT_INVALID);
    assert(active==3);
    img->pages[0].virtual_page=old_va;

    img->pages[0].virtual_page=NOVA_USER_STACK_TOP-4096u;
    assert(nova_user_space_prepare(&space,&ops,img)==NOVA_PT_INVALID);
    img->pages[0].virtual_page=old_va;

    allocation_budget=4u; /* forces failure after page-table construction */
    assert(nova_user_space_prepare(&space,&ops,img)==NOVA_PT_OUT_OF_MEMORY);
    assert(active==3&&!space.prepared&&space.stack_count==0);
    allocation_budget=UINT32_MAX;

    struct nova_pt_ops incomplete=ops;
    incomplete.physical_address=NULL;
    assert(nova_user_space_prepare(&space,&incomplete,img)==NOVA_PT_INVALID);
    assert(nova_user_space_prepare(NULL,&ops,img)==NOVA_PT_INVALID);
    assert(nova_user_space_prepare(&space,&ops,NULL)==NOVA_PT_INVALID);
    assert(nova_process_drop(&p,pid,release,NULL));
    assert(active==0);
}
static void test_distinct_stack_frames(void){
    image_fixture();
    struct nova_process_table p={0};
    uint32_t a=stage(&p),b=stage(&p);
    struct nova_user_space x={0},y={0};
    assert(nova_user_space_prepare(&x,&ops,nova_process_find(&p,a))==NOVA_PT_OK);
    assert(nova_user_space_prepare(&y,&ops,nova_process_find(&p,b))==NOVA_PT_OK);
    assert(x.page_tables.root_physical!=y.page_tables.root_physical);
    uint64_t a_phys=0,b_phys=0,a_flags=0,b_flags=0;
    assert(nova_pt_lookup(&x.page_tables,NOVA_USER_STACK_TOP-4096u,&a_phys,&a_flags));
    assert(nova_pt_lookup(&y.page_tables,NOVA_USER_STACK_TOP-4096u,&b_phys,&b_flags));
    assert(a_phys!=b_phys && a_flags==b_flags);
    assert(nova_user_stack_guard_is_unmapped(&x));
    assert(nova_user_stack_guard_is_unmapped(&y));
    nova_user_space_destroy(&x,&ops);
    assert(nova_user_stack_guard_is_unmapped(&y));
    nova_user_space_destroy(&y,&ops);
    assert(nova_process_drop(&p,a,release,NULL));
    assert(nova_process_drop(&p,b,release,NULL));
    assert(active==0);
}

/* Reproduce broken allocator ownership hand-offs during offline stack
 * construction. A suspicious frame must never be wiped, registered or freed
 * by rollback if it aliases a staged image, PT or accepted stack frame. */
enum stack_fault_mode {
    STACK_BORROW_IMAGE,
    STACK_BORROW_TABLE,
    STACK_BORROW_PREVIOUS,
    TABLE_BORROW_PENDING_STACK,
    STACK_PHYS_ALIAS_IMAGE,
    STACK_PHYS_ALIAS_PREVIOUS,
    STACK_PHYS_UNKNOWN
};
struct stack_fault_context {
    enum stack_fault_mode mode;
    unsigned calls;
    unsigned inject_at;
    void *image_frame;
    void *first_table;
    void *first_stack;
    void *fresh_suspect;
};
static void *stack_fault_allocate(void *opaque){
    struct stack_fault_context *ctx=(struct stack_fault_context*)opaque;
    ctx->calls++;
    if(ctx->calls==ctx->inject_at){
        switch(ctx->mode){
            case STACK_BORROW_IMAGE:return ctx->image_frame;
            case STACK_BORROW_TABLE:return ctx->first_table;
            case STACK_BORROW_PREVIOUS:return ctx->first_stack;
            case TABLE_BORROW_PENDING_STACK:return ctx->first_stack;
            case STACK_PHYS_ALIAS_IMAGE:
            case STACK_PHYS_ALIAS_PREVIOUS:
            case STACK_PHYS_UNKNOWN:
                ctx->fresh_suspect=allocate(NULL);
                return ctx->fresh_suspect;
        }
    }
    void *p=allocate(NULL);
    if(ctx->calls==1)ctx->first_table=p;
    if(ctx->calls==5)ctx->first_stack=p;
    return p;
}
static bool stack_fault_physical(void *opaque,const void *frame,uint64_t *out){
    struct stack_fault_context *ctx=(struct stack_fault_context*)opaque;
    if(frame && frame==ctx->fresh_suspect){
        if(ctx->mode==STACK_PHYS_UNKNOWN)return false;
        if(ctx->mode==STACK_PHYS_ALIAS_IMAGE)
            return physical(NULL,ctx->image_frame,out);
        if(ctx->mode==STACK_PHYS_ALIAS_PREVIOUS)
            return physical(NULL,ctx->first_stack,out);
    }
    return physical(NULL,frame,out);
}
static void test_stack_allocator_ownership(void){
    const enum stack_fault_mode modes[]={
        STACK_BORROW_IMAGE,STACK_BORROW_TABLE,STACK_BORROW_PREVIOUS,
        TABLE_BORROW_PENDING_STACK,STACK_PHYS_ALIAS_IMAGE,
        STACK_PHYS_ALIAS_PREVIOUS,STACK_PHYS_UNKNOWN
    };
    for(size_t k=0;k<sizeof(modes)/sizeof(modes[0]);k++){
        image_fixture();
        struct nova_process_table processes={0};
        uint32_t pid=stage(&processes);
        struct nova_process_image *image=nova_process_find(&processes,pid);
        assert(image && active==3);
        uint8_t original[32];
        memcpy(original,image->pages[0].frame,sizeof(original));
        struct stack_fault_context fault={0};
        fault.mode=modes[k];
        fault.image_frame=image->pages[0].frame;
        fault.inject_at=(fault.mode==STACK_BORROW_PREVIOUS ||
                         fault.mode==STACK_PHYS_ALIAS_PREVIOUS)?9u:
                        (fault.mode==TABLE_BORROW_PENDING_STACK?6u:5u);
        struct nova_pt_ops injected={
            .context=&fault,.allocate=stack_fault_allocate,
            .release=release,.physical_address=stack_fault_physical
        };
        struct nova_user_space space={0};
        assert(nova_user_space_prepare(&space,&injected,image)==NOVA_PT_BAD_FRAME);
        assert(fault.calls==fault.inject_at);
        assert(!space.prepared && !space.stack_count &&
               !space.page_tables.tables_used);
        assert(!memcmp(original,image->pages[0].frame,sizeof(original)));
        /* A new frame with a fraudulent or missing physical identity is
         * deliberately quarantined rather than released without proof.
         * The test knows its true pool ownership and reclaims it explicitly. */
        assert(active==(fault.fresh_suspect?4u:3u));
        if(fault.fresh_suspect)release(NULL,fault.fresh_suspect);
        assert(active==3);
        assert(nova_process_drop(&processes,pid,release,NULL));
        assert(active==0);
    }
}

/* Access validation must use the offline mapping manifest, not raw
 * possibly corrupted PTEs. Deliberately mutate only simulated host memory.
 * No CR3 changes, CPU page faults or user execution are involved.
 */
static uint64_t *raw_pte(struct nova_page_table *tree,uint64_t va){
    uint64_t *entries=nova_pt_find(tree,tree->root_physical);
    assert(entries);
    for(unsigned level=0;level<3;level++){
        unsigned shift=39u-9u*level;
        uint64_t entry=entries[(va>>shift)&511u];
        assert(entry&NOVA_PT_P);
        entries=nova_pt_find(tree,entry&NOVA_PT_ADDR);
        assert(entries);
    }
    return &entries[(va>>12)&511u];
}
static void test_offline_pte_integrity(void){
    image_fixture();
    struct nova_process_table processes={0};
    uint32_t pid=stage(&processes);
    struct nova_process_image *img=nova_process_find(&processes,pid);
    assert(img && active==3);
    struct nova_user_space space={0};
    assert(nova_user_space_prepare(&space,&ops,img)==NOVA_PT_OK);
    struct nova_page_table *tree=&space.page_tables;
    assert(nova_user_stack_guard_is_unmapped(&space));
    assert(nova_pt_slot_empty(tree,NOVA_USER_STACK_GUARD));
    assert(!nova_pt_slot_empty(tree,0x401000));
    uint64_t phys=0,attr=0;
    uint64_t *code=raw_pte(tree,0x401000);
    uint64_t *data=raw_pte(tree,0x402000);
    uint64_t *guard=raw_pte(tree,NOVA_USER_STACK_GUARD);
    uint64_t *stack=raw_pte(tree,NOVA_USER_STACK_TOP-3u*4096u);
    uint64_t saved_code=*code,saved_data=*data,saved_stack=*stack;
    assert(*guard==0);
    assert(nova_user_span_accessible(&space,0x401000,8,false));
    assert(nova_user_span_accessible(&space,0x402000,8,true));

    /* Escalating RX code to RW must not authorize a copyout. */
    *code|=NOVA_PT_RW;
    assert(!nova_pt_lookup(tree,0x401000,&phys,&attr));
    assert(!nova_user_span_accessible(&space,0x401000,8,false));
    assert(!nova_user_span_accessible(&space,0x401000,8,true));
    *code=saved_code;

    /* Removing NX from RW data must fail closed, even for a read. */
    *data&=~NOVA_PT_NX;
    assert(!nova_pt_lookup(tree,0x402000,&phys,&attr));
    assert(!nova_user_span_accessible(&space,0x402000,8,false));
    *data=saved_data;

    /* An attacker must not replace an RX frame with a different owned
     * frame without changing the manifest. */
    *code=(*code&~NOVA_PT_ADDR)|(saved_data&NOVA_PT_ADDR);
    assert(!nova_pt_lookup(tree,0x401000,&phys,&attr));
    *code=saved_code;

    /* A present or merely nonzero guard PTE is never an empty slot.
     * An unregistered PTE must not be readable through the validator. */
    *guard=saved_data;
    assert(!nova_pt_lookup(tree,NOVA_USER_STACK_GUARD,&phys,&attr));
    assert(!nova_pt_slot_empty(tree,NOVA_USER_STACK_GUARD));
    assert(!nova_user_stack_guard_is_unmapped(&space));
    assert(!nova_user_span_accessible(&space,NOVA_USER_STACK_GUARD,8,false));
    *guard=NOVA_PT_NX;
    assert(!nova_pt_slot_empty(tree,NOVA_USER_STACK_GUARD));
    assert(!nova_user_stack_guard_is_unmapped(&space));
    *guard=0;
    assert(nova_pt_slot_empty(tree,NOVA_USER_STACK_GUARD));

    /* The guard validator must check all four leaves, not just the top. */
    *stack&=~NOVA_PT_NX;
    assert(!nova_user_stack_guard_is_unmapped(&space));
    *stack=saved_stack;

    /* Parent rights and unexpected bits are also integrity failures. */
    uint64_t *root=nova_pt_find(tree,tree->root_physical);
    assert(root);
    uint64_t *pml4e=&root[(UINT64_C(0x401000)>>39)&511u];
    uint64_t saved_parent=*pml4e;
    *pml4e&=~NOVA_PT_US;
    assert(!nova_pt_lookup(tree,0x401000,&phys,&attr));
    *pml4e=saved_parent|NOVA_PT_NX;
    assert(!nova_pt_lookup(tree,0x401000,&phys,&attr));
    *pml4e=saved_parent;

    /* A damaged manifest or invalid count must never cause OOB access. */
    uint64_t saved_entry=tree->mapped_entry[0];
    tree->mapped_entry[0]^=NOVA_PT_RW;
    assert(!nova_pt_lookup(tree,0x401000,&phys,&attr));
    tree->mapped_entry[0]=saved_entry;
    uint16_t saved_count=tree->mapped_pages;
    tree->mapped_pages=UINT16_MAX;
    assert(!nova_pt_lookup(tree,0x401000,&phys,&attr));
    assert(!nova_pt_slot_empty(tree,NOVA_USER_STACK_GUARD));
    tree->mapped_pages=saved_count;
    saved_count=tree->tables_used;
    tree->tables_used=UINT16_MAX;
    assert(!nova_pt_lookup(tree,0x401000,&phys,&attr));
    assert(!nova_pt_slot_empty(tree,NOVA_USER_STACK_GUARD));
    tree->tables_used=saved_count;

    assert(nova_pt_lookup(tree,0x401000,&phys,&attr));
    assert(nova_user_span_accessible(&space,0x402000,8,true));
    assert(nova_user_stack_guard_is_unmapped(&space));
    nova_user_space_destroy(&space,&ops);
    assert(active==3);
    assert(nova_process_drop(&processes,pid,release,NULL));
    assert(active==0);
}

/* Destruction is a security boundary too: damaged metadata must not
 * cause an out-of-bounds walk, double free, or release of an ELF frame.
 * Save the valid descriptor and restore it after each rejected teardown
 * to reclaim the deliberately leaked host-test pages at the end.
 */
static void test_corrupt_teardown_metadata(void){
    image_fixture();
    struct nova_process_table processes={0};
    uint32_t pid=stage(&processes);
    struct nova_process_image *img=nova_process_find(&processes,pid);
    assert(img && active==3);
    struct nova_page_table tree={0};
    assert(nova_pt_prepare(&tree,&ops,img)==NOVA_PT_OK);
    struct nova_page_table original=tree;
    uint32_t baseline=active;
    assert(nova_pt_tables_releasable(&tree,&ops));

    tree.tables_used=UINT16_MAX;
    assert(!nova_pt_tables_releasable(&tree,&ops));
    nova_pt_destroy(&tree,&ops);
    assert(active==baseline && tree.tables_used==0);
    tree=original;

    tree.protected_count=UINT16_MAX;
    nova_pt_destroy(&tree,&ops);
    assert(active==baseline);
    tree=original;

    tree.mapped_pages=UINT16_MAX;
    nova_pt_destroy(&tree,&ops);
    assert(active==baseline);
    tree=original;

    tree.tables[1]=tree.tables[0];
    nova_pt_destroy(&tree,&ops);
    assert(active==baseline);
    tree=original;

    tree.table_physical[1]=tree.table_physical[0];
    nova_pt_destroy(&tree,&ops);
    assert(active==baseline);
    tree=original;

    tree.tables[1]=img->pages[0].frame;
    tree.table_physical[1]=original.mapped_physical[0];
    nova_pt_destroy(&tree,&ops);
    uint64_t still_owned=0;
    assert(active==baseline &&
           physical(NULL,img->pages[0].frame,&still_owned));
    tree=original;

    tree.root_physical^=NOVA_PT_PAGE_SIZE;
    nova_pt_destroy(&tree,&ops);
    assert(active==baseline);
    tree=original;

    tree.pending_leaf_frame=img->pages[0].frame;
    nova_pt_destroy(&tree,&ops);
    assert(active==baseline);
    tree=original;

    struct nova_pt_ops missing_physical=ops;
    missing_physical.physical_address=NULL;
    nova_pt_destroy(&tree,&missing_physical);
    assert(active==baseline);
    tree=original;

    assert(nova_pt_tables_releasable(&tree,&ops));
    nova_pt_destroy(&tree,&ops);
    assert(active==3);

    struct nova_user_space space={0};
    assert(nova_user_space_prepare(&space,&ops,img)==NOVA_PT_OK);
    struct nova_user_space saved=space;
    baseline=active;
    assert(nova_user_space_frames_releasable(&space,&ops));

    space.stack_count=UINT32_MAX;
    assert(!nova_user_space_frames_releasable(&space,&ops));
    nova_user_space_destroy(&space,&ops);
    assert(active==baseline && space.stack_count==0);
    space=saved;

    space.stack_frames[1]=space.stack_frames[0];
    nova_user_space_destroy(&space,&ops);
    assert(active==baseline);
    space=saved;

    space.stack_physical[1]=space.stack_physical[0];
    nova_user_space_destroy(&space,&ops);
    assert(active==baseline);
    space=saved;

    space.stack_frames[1]=img->pages[0].frame;
    space.stack_physical[1]=saved.page_tables.protected_physical[0];
    nova_user_space_destroy(&space,&ops);
    assert(active==baseline);
    space=saved;

    space.stack_frames[1]=space.page_tables.tables[0];
    space.stack_physical[1]=space.page_tables.table_physical[0];
    nova_user_space_destroy(&space,&ops);
    assert(active==baseline);
    space=saved;

    space.page_tables.tables_used=UINT16_MAX;
    nova_user_space_destroy(&space,&ops);
    assert(active==baseline);
    space=saved;

    nova_user_space_destroy(&space,&missing_physical);
    assert(active==baseline);
    space=saved;

    assert(nova_user_space_frames_releasable(&space,&ops));
    nova_user_space_destroy(&space,&ops);
    assert(active==3);
    assert(nova_process_drop(&processes,pid,release,NULL));
    assert(active==0);
}

int main(void){
    test_page_tables();
    test_isolated_roots();
    test_failures();
    test_physical_alias_guards();
    test_guarded_user_stack();
    test_stack_failures();
    test_distinct_stack_frames();
    test_stack_allocator_ownership();
    test_offline_pte_integrity();
    test_corrupt_teardown_metadata();
    assert(active==0);
    puts("Offline paging: PML4, guarded user stacks, W^X/NX, rollback, separate roots; no CR3 use.");
    return 0;
}
