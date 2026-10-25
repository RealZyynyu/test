/* Host-only PMM role ledger + offline ELF/table/stack integration tests.
 * No CPU registers, disk access, ISO, or program execution are involved.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/pmm.h"
#include "../kernel/user_space.h"

#define TEST_FRAMES 48u
#define IMAGE_BYTES 8192u
static _Alignas(4096) uint8_t ram[TEST_FRAMES][4096];
static uint8_t elf[IMAGE_BYTES];

static void reset_pmm(void){
    nova_pmm=(struct nova_page_manager){0};
    memset(ram,0xA5,sizeof(ram));
    struct limine_memmap_entry region={
        .base=0x200000u,.length=TEST_FRAMES*4096u,.type=LIMINE_MEMMAP_USABLE
    };
    struct limine_memmap_entry *entries[]={&region};
    struct limine_memmap_response map={.entry_count=1,.entries=entries};
    struct limine_hhdm_response hhdm={0};
    assert((uintptr_t)ram>=0x200000u);
    hhdm.offset=(uint64_t)((uintptr_t)ram-0x200000u);
    assert(nova_pmm_init(&map,&hhdm));
    assert(nova_pmm_metadata_valid());
}
static void *alloc_image(void *ctx){
    (void)ctx;return nova_page_alloc_role(NOVA_FRAME_IMAGE);
}
static void free_image(void *ctx,void *frame){
    (void)ctx;assert(nova_page_free_role(frame,NOVA_FRAME_IMAGE));
}
static bool phys_image(void *ctx,const void *frame,uint64_t *out){
    (void)ctx;return nova_pmm_physical_role(frame,NOVA_FRAME_IMAGE,out);
}
static void *alloc_table(void *ctx){
    (void)ctx;return nova_page_alloc_role(NOVA_FRAME_PAGE_TABLE);
}
static void free_table(void *ctx,void *frame){
    (void)ctx;assert(nova_page_free_role(frame,NOVA_FRAME_PAGE_TABLE));
}
static bool phys_table(void *ctx,const void *frame,uint64_t *out){
    (void)ctx;return nova_pmm_physical_role(frame,NOVA_FRAME_PAGE_TABLE,out);
}
static void *alloc_stack(void *ctx){
    (void)ctx;return nova_page_alloc_role(NOVA_FRAME_STACK);
}
static void free_stack(void *ctx,void *frame){
    (void)ctx;assert(nova_page_free_role(frame,NOVA_FRAME_STACK));
}
static bool phys_stack(void *ctx,const void *frame,uint64_t *out){
    (void)ctx;return nova_pmm_physical_role(frame,NOVA_FRAME_STACK,out);
}
static const struct nova_pt_ops ops={
    .context=NULL,
    .allocate=alloc_table,
    .release=free_table,
    .physical_address=phys_image,
    .table_physical_address=phys_table,
    .stack_allocate=alloc_stack,
    .stack_release=free_stack,
    .stack_physical_address=phys_stack
};
static void w16(size_t pos,uint16_t value){
    elf[pos]=(uint8_t)value;elf[pos+1]=(uint8_t)(value>>8);
}
static void w32(size_t pos,uint32_t value){
    for(unsigned i=0;i<4;i++)elf[pos+i]=(uint8_t)(value>>(8u*i));
}
static void w64(size_t pos,uint64_t value){
    for(unsigned i=0;i<8;i++)elf[pos+i]=(uint8_t)(value>>(8u*i));
}
static void fixture(void){
    memset(elf,0,sizeof(elf));
    elf[0]=0x7f;elf[1]='E';elf[2]='L';elf[3]='F';
    elf[4]=2;elf[5]=1;elf[6]=1;
    w16(16,2);w16(18,62);w32(20,1);
    w64(24,0x401000u);w64(32,64u);
    w16(52,64);w16(54,56);w16(56,1);
    w32(64,1);w32(68,5); /* PT_LOAD, RX */
    w64(72,4096u);w64(80,0x401000u);
    w64(96,64u);w64(104,4096u);w64(112,4096u);
    for(unsigned i=0;i<64;i++)elf[4096+i]=(uint8_t)(i+1u);
}
static bool read_elf(void *ctx,uint64_t pos,uint8_t *out,size_t n){
    (void)ctx;
    if(pos>IMAGE_BYTES||n>IMAGE_BYTES-pos)return false;
    memcpy(out,elf+(size_t)pos,n);
    return true;
}
static void test_role_transitions_and_corruption(void){
    reset_pmm();
    assert(nova_page_alloc_role(NOVA_FRAME_FREE)==NULL);
    assert(nova_page_alloc_role((enum nova_frame_role)255)==NULL);
    uint8_t *image=(uint8_t*)nova_page_alloc_role(NOVA_FRAME_IMAGE);
    uint8_t *table=(uint8_t*)nova_page_alloc_role(NOVA_FRAME_PAGE_TABLE);
    uint8_t *stack=(uint8_t*)nova_page_alloc_role(NOVA_FRAME_STACK);
    assert(image==ram[0] && table==ram[1] && stack==ram[2]);
    enum nova_frame_role role=NOVA_FRAME_FREE;
    assert(nova_pmm_frame_role(image,&role)&&role==NOVA_FRAME_IMAGE);
    assert(nova_pmm_frame_role(table,&role)&&role==NOVA_FRAME_PAGE_TABLE);
    assert(nova_pmm_frame_role(stack,&role)&&role==NOVA_FRAME_STACK);
    assert(!nova_pmm_frame_role(ram[3],&role));
    uint64_t phys=0;
    assert(nova_pmm_physical_role(image,NOVA_FRAME_IMAGE,&phys)&&phys==0x200000u);
    phys=0xBADu;
    assert(!nova_pmm_physical_role(image,NOVA_FRAME_STACK,&phys)&&phys==0xBADu);
    assert(!nova_page_free_role(image,NOVA_FRAME_PAGE_TABLE));
    assert(!nova_page_free(image)); /* legacy API can free GENERAL only */
    memset(image,0x5C,4096);
    assert(!nova_page_free_role(image,NOVA_FRAME_STACK));
    for(unsigned i=0;i<4096;i++)assert(image[i]==0x5C);
    const struct nova_page_manager good=nova_pmm;
    for(unsigned fault=0;fault<4;fault++){
        nova_pmm=good;
        switch(fault){
            case 0:nova_pmm.roles[0]=NOVA_FRAME_FREE;break;
            case 1:nova_pmm.roles[0]=255;break;
            case 2:nova_pmm.roles[3]=NOVA_FRAME_IMAGE;break;
            case 3:nova_pmm.roles[NOVA_PAGE_POOL_CAP-1u]=NOVA_FRAME_STACK;break;
            default:assert(false);
        }
        assert(!nova_pmm_metadata_valid());
        assert(nova_page_alloc_role(NOVA_FRAME_IMAGE)==NULL);
        assert(!nova_page_free_role(image,NOVA_FRAME_IMAGE));
        assert(!nova_pmm_physical_role(image,NOVA_FRAME_IMAGE,&phys));
        for(unsigned i=0;i<4096;i++)assert(image[i]==0x5C);
    }
    nova_pmm=good;
    assert(nova_pmm_metadata_valid());
    assert(nova_page_free_role(image,NOVA_FRAME_IMAGE));
    for(unsigned i=0;i<4096;i++)assert(image[i]==0);
    assert(!nova_pmm_frame_role(image,&role));
    /* Recycled pointer with a DIFFERENT role cannot free its new owner. */
    uint8_t *recycled=(uint8_t*)nova_page_alloc_role(NOVA_FRAME_STACK);
    assert(recycled==image);
    memset(recycled,0x73,4096);
    assert(!nova_page_free_role(image,NOVA_FRAME_IMAGE));
    for(unsigned i=0;i<4096;i++)assert(recycled[i]==0x73);
    assert(nova_page_free_role(recycled,NOVA_FRAME_STACK));
    assert(nova_page_free_role(table,NOVA_FRAME_PAGE_TABLE));
    assert(nova_page_free_role(stack,NOVA_FRAME_STACK));
    assert(nova_free_pages()==TEST_FRAMES);
}
static void test_offline_role_integration(void){
    reset_pmm();
    assert(nova_pmm_self_test());
    assert(!strcmp(nova_elf_result_name(NOVA_ELF_OK),
                   "VALID STATIC ELF64 (NOT EXECUTED)"));
    fixture();
    struct nova_process_table processes={0};
    uint32_t pid=0;
    assert(nova_process_stage(&processes,"RAM:/ROLE.ELF",read_elf,NULL,
        IMAGE_BYTES,alloc_image,free_image,phys_image,NULL,&pid)==NOVA_PROCESS_OK);
    struct nova_process_image *image=nova_process_find(&processes,pid);
    assert(image && image->page_count==1);
    enum nova_frame_role role=NOVA_FRAME_FREE;
    assert(nova_pmm_frame_role(image->pages[0].frame,&role)&&role==NOVA_FRAME_IMAGE);
    struct nova_user_space space={0};
    struct nova_pt_ops partial=ops;
    partial.stack_release=NULL;
    const uint32_t before=nova_pmm.in_use;
    assert(nova_user_space_prepare(&space,&partial,image)==NOVA_PT_INVALID);
    assert(nova_pmm.in_use==before);
    assert(nova_user_space_prepare(&space,&ops,image)==NOVA_PT_OK);
    assert(nova_user_stack_guard_is_unmapped(&space));
    assert(space.stack_count==NOVA_USER_STACK_PAGES);
    assert(space.page_tables.tables_used>0);
    assert(nova_pmm.in_use==image->page_count+space.stack_count+
                            space.page_tables.tables_used);
    for(uint16_t i=0;i<space.page_tables.tables_used;i++){
        void *p=space.page_tables.tables[i];
        assert(nova_pmm_frame_role(p,&role)&&role==NOVA_FRAME_PAGE_TABLE);
        assert(!nova_pmm_physical_role(p,NOVA_FRAME_IMAGE,&(uint64_t){0}));
    }
    for(uint32_t i=0;i<space.stack_count;i++){
        void *p=space.stack_frames[i];
        assert(nova_pmm_frame_role(p,&role)&&role==NOVA_FRAME_STACK);
        assert(!nova_pmm_physical_role(p,NOVA_FRAME_PAGE_TABLE,&(uint64_t){0}));
    }
    /* A syntactically valid but forged role must not pass cleanup checks.
     * Restore the host fixture before actual cleanup to avoid test leaks. */
    const uintptr_t base=(uintptr_t)ram;
    const uintptr_t table_off=(uintptr_t)space.page_tables.tables[0]-base;
    const uintptr_t stack_off=(uintptr_t)space.stack_frames[0]-base;
    const uintptr_t image_off=(uintptr_t)image->pages[0].frame-base;
    assert(table_off%4096u==0 && stack_off%4096u==0 && image_off%4096u==0);
    nova_pmm.roles[table_off/4096u]=NOVA_FRAME_IMAGE;
    assert(nova_pmm_metadata_valid());
    assert(!nova_pt_tables_releasable(&space.page_tables,&ops));
    assert(!nova_user_space_frames_releasable(&space,&ops));
    nova_pmm.roles[table_off/4096u]=NOVA_FRAME_PAGE_TABLE;
    nova_pmm.roles[stack_off/4096u]=NOVA_FRAME_PAGE_TABLE;
    assert(!nova_user_space_frames_releasable(&space,&ops));
    nova_pmm.roles[stack_off/4096u]=NOVA_FRAME_STACK;
    nova_pmm.roles[image_off/4096u]=NOVA_FRAME_STACK;
    assert(!nova_process_frames_releasable(image,&processes,phys_image,NULL));
    nova_pmm.roles[image_off/4096u]=NOVA_FRAME_IMAGE;
    assert(nova_user_space_frames_releasable(&space,&ops));
    assert(nova_process_frames_releasable(image,&processes,phys_image,NULL));
    nova_user_space_destroy(&space,&ops);
    assert(nova_process_drop(&processes,pid,free_image,phys_image,NULL));
    assert(nova_pmm.in_use==0 && nova_free_pages()==TEST_FRAMES);
    assert(nova_pmm_metadata_valid());
}

/* Real PMM generation tickets for offline stacks. These tests deliberately
 * do not execute ELF images or activate the prepared page tables. */
static unsigned ticket_budget=UINT32_MAX;
static struct nova_frame_ticket forged_ticket;
static bool ticket_stack_alloc(void *ctx,void **frame,uint64_t *generation){
    (void)ctx;
    if(!frame||!generation||ticket_budget==0)return false;
    struct nova_frame_ticket t={0};
    if(!nova_page_alloc_ticket(NOVA_FRAME_STACK,&t))return false;
    if(ticket_budget!=UINT32_MAX)ticket_budget--;
    *frame=t.frame;*generation=t.generation;
    return true;
}
static bool ticket_stack_phys(void *ctx,const void *frame,
                              uint64_t generation,uint64_t *out){
    (void)ctx;
    struct nova_frame_ticket t={
        .frame=(void *)frame,.generation=generation,.role=NOVA_FRAME_STACK
    };
    return nova_pmm_physical_ticket(&t,out);
}
static bool ticket_stack_free(void *ctx,void *frame,uint64_t generation){
    (void)ctx;
    struct nova_frame_ticket t={
        .frame=frame,.generation=generation,.role=NOVA_FRAME_STACK
    };
    return nova_page_free_ticket(&t);
}
static struct nova_pt_ops ticket_ops(void){
    struct nova_pt_ops result=ops;
    result.stack_ticket_allocate=ticket_stack_alloc;
    result.stack_ticket_physical_address=ticket_stack_phys;
    result.stack_ticket_release=ticket_stack_free;
    return result;
}
static uint32_t stage_ticket_fixture(struct nova_process_table *processes){
    fixture();
    uint32_t pid=0;
    assert(nova_process_stage(processes,"RAM:/STACK.ELF",read_elf,NULL,
        IMAGE_BYTES,alloc_image,free_image,phys_image,NULL,&pid)==NOVA_PROCESS_OK);
    return pid;
}
static void test_guarded_stack_lifecycle(void){
    reset_pmm();
    ticket_budget=UINT32_MAX;
    struct nova_process_table processes={0};
    uint32_t pid=stage_ticket_fixture(&processes);
    struct nova_process_image *image=nova_process_find(&processes,pid);
    struct nova_pt_ops guarded=ticket_ops();
    struct nova_pt_ops partial=guarded;
    partial.stack_ticket_release=NULL;
    struct nova_user_space space={0};
    uint32_t before=nova_pmm.in_use;
    assert(nova_user_space_prepare(&space,&partial,image)==NOVA_PT_INVALID);
    assert(nova_pmm.in_use==before);
    assert(nova_user_space_prepare(&space,&guarded,image)==NOVA_PT_OK);
    assert(space.stack_guarded && nova_user_stack_guard_is_unmapped(&space));
    for(uint32_t i=0;i<space.stack_count;i++){
        assert(space.stack_generation[i]!=0);
        struct nova_frame_ticket t={
            .frame=space.stack_frames[i],.generation=space.stack_generation[i],
            .role=NOVA_FRAME_STACK
        };
        assert(nova_pmm_ticket_valid(&t));
    }
    /* A caller without generation callbacks must not release these frames. */
    assert(!nova_user_space_frames_releasable(&space,&ops));
    assert(nova_user_space_frames_releasable(&space,&guarded));
    nova_user_space_destroy(&space,&guarded);
    assert(nova_pmm.in_use==image->page_count);
    assert(nova_process_drop(&processes,pid,free_image,phys_image,NULL));
    assert(nova_pmm.in_use==0 && nova_pmm_metadata_valid());
}
static void test_stale_stack_ticket_blocks_all_cleanup(void){
    reset_pmm();
    ticket_budget=UINT32_MAX;
    struct nova_process_table processes={0};
    uint32_t pid=stage_ticket_fixture(&processes);
    struct nova_process_image *image=nova_process_find(&processes,pid);
    struct nova_pt_ops guarded=ticket_ops();
    struct nova_user_space space={0};
    assert(nova_user_space_prepare(&space,&guarded,image)==NOVA_PT_OK);
    struct nova_user_space original=space;
    struct nova_frame_ticket stale={
        .frame=space.stack_frames[0],.generation=space.stack_generation[0],
        .role=NOVA_FRAME_STACK
    };
    assert(nova_page_free_ticket(&stale));
    struct nova_frame_ticket replacement={0};
    assert(nova_page_alloc_ticket(NOVA_FRAME_STACK,&replacement));
    assert(replacement.frame==stale.frame);
    assert(replacement.generation!=stale.generation);
    memset(replacement.frame,0x6B,4096);
    uint32_t before=nova_pmm.in_use;
    assert(!nova_user_space_frames_releasable(&space,&guarded));
    nova_user_space_destroy(&space,&guarded);
    assert(nova_pmm.in_use==before);
    assert(nova_pmm_ticket_valid(&replacement));
    for(unsigned i=0;i<4096;i++)assert(((uint8_t *)replacement.frame)[i]==0x6B);
    /* The test fixture alone knows which remaining allocations are ours. */
    for(uint32_t i=1;i<original.stack_count;i++)
        assert(ticket_stack_free(NULL,original.stack_frames[i],
                                 original.stack_generation[i]));
    nova_pt_destroy(&original.page_tables,&guarded);
    assert(nova_page_free_ticket(&replacement));
    assert(nova_process_drop(&processes,pid,free_image,phys_image,NULL));
    assert(nova_pmm.in_use==0);
}
static void test_corrupt_stack_ticket_and_rollback(void){
    reset_pmm();
    ticket_budget=UINT32_MAX;
    struct nova_process_table processes={0};
    uint32_t pid=stage_ticket_fixture(&processes);
    struct nova_process_image *image=nova_process_find(&processes,pid);
    struct nova_pt_ops guarded=ticket_ops();
    struct nova_user_space space={0};
    assert(nova_user_space_prepare(&space,&guarded,image)==NOVA_PT_OK);
    struct nova_user_space original=space;
    uint32_t before=nova_pmm.in_use;
    space.stack_generation[1]++;
    assert(!nova_user_space_frames_releasable(&space,&guarded));
    nova_user_space_destroy(&space,&guarded);
    assert(nova_pmm.in_use==before);
    space=original;
    nova_user_space_destroy(&space,&guarded);
    assert(nova_pmm.in_use==image->page_count);
    ticket_budget=1;
    assert(nova_user_space_prepare(&space,&guarded,image)==NOVA_PT_OUT_OF_MEMORY);
    assert(nova_pmm.in_use==image->page_count);
    ticket_budget=UINT32_MAX;
    assert(nova_process_drop(&processes,pid,free_image,phys_image,NULL));
    assert(nova_pmm.in_use==0);
}
static bool forge_stack_alloc(void *ctx,void **frame,uint64_t *generation){
    if(!ticket_stack_alloc(ctx,frame,generation))return false;
    forged_ticket=(struct nova_frame_ticket){
        .frame=*frame,.generation=*generation,.role=NOVA_FRAME_STACK
    };
    memset(*frame,0xA7,4096);
    (*generation)++;
    return true;
}
static void test_forged_stack_ticket_never_zeroed(void){
    reset_pmm();
    ticket_budget=UINT32_MAX;
    forged_ticket=(struct nova_frame_ticket){0};
    struct nova_process_table processes={0};
    uint32_t pid=stage_ticket_fixture(&processes);
    struct nova_process_image *image=nova_process_find(&processes,pid);
    struct nova_pt_ops guarded=ticket_ops();
    guarded.stack_ticket_allocate=forge_stack_alloc;
    struct nova_user_space space={0};
    assert(nova_user_space_prepare(&space,&guarded,image)==NOVA_PT_BAD_FRAME);
    assert(!space.stack_count&&!space.prepared);
    assert(nova_pmm_ticket_valid(&forged_ticket));
    for(unsigned i=0;i<4096;i++)
        assert(((uint8_t *)forged_ticket.frame)[i]==0xA7);
    assert(nova_pmm.in_use==image->page_count+1u);
    assert(nova_page_free_ticket(&forged_ticket));
    assert(nova_process_drop(&processes,pid,free_image,phys_image,NULL));
    assert(nova_pmm.in_use==0);
}

/* Generation-checked PAGE_TABLE frames: no real CR3 or user execution. */
static unsigned table_ticket_budget=UINT32_MAX;
static struct nova_frame_ticket forged_table_ticket;
static bool ticket_table_alloc(void *ctx,void **frame,uint64_t *generation){
    (void)ctx;
    if(!frame||!generation||table_ticket_budget==0)return false;
    struct nova_frame_ticket t={0};
    if(!nova_page_alloc_ticket(NOVA_FRAME_PAGE_TABLE,&t))return false;
    if(table_ticket_budget!=UINT32_MAX)table_ticket_budget--;
    *frame=t.frame;*generation=t.generation;
    return true;
}
static bool ticket_table_phys(void *ctx,const void *frame,
                              uint64_t generation,uint64_t *out){
    (void)ctx;
    struct nova_frame_ticket t={
        .frame=(void *)frame,.generation=generation,.role=NOVA_FRAME_PAGE_TABLE
    };
    return nova_pmm_physical_ticket(&t,out);
}
static bool ticket_table_free(void *ctx,void *frame,uint64_t generation){
    (void)ctx;
    struct nova_frame_ticket t={
        .frame=frame,.generation=generation,.role=NOVA_FRAME_PAGE_TABLE
    };
    return nova_page_free_ticket(&t);
}
static struct nova_pt_ops fully_guarded_ops(void){
    struct nova_pt_ops result=ticket_ops();
    result.table_ticket_allocate=ticket_table_alloc;
    result.table_ticket_physical_address=ticket_table_phys;
    result.table_ticket_release=ticket_table_free;
    return result;
}
static void test_guarded_table_lifecycle(void){
    reset_pmm();
    table_ticket_budget=UINT32_MAX;
    ticket_budget=UINT32_MAX;
    struct nova_process_table processes={0};
    uint32_t pid=stage_ticket_fixture(&processes);
    struct nova_process_image *image=nova_process_find(&processes,pid);
    struct nova_pt_ops guarded=fully_guarded_ops();
    struct nova_pt_ops partial=guarded;
    partial.table_ticket_release=NULL;
    struct nova_user_space space={0};
    const uint32_t before=nova_pmm.in_use;
    assert(nova_user_space_prepare(&space,&partial,image)==NOVA_PT_INVALID);
    assert(nova_pmm.in_use==before);
    assert(nova_user_space_prepare(&space,&guarded,image)==NOVA_PT_OK);
    assert(space.page_tables.tables_guarded && space.stack_guarded);
    assert(nova_user_stack_guard_is_unmapped(&space));
    for(uint16_t i=0;i<space.page_tables.tables_used;i++){
        assert(space.page_tables.table_generation[i]!=0);
        struct nova_frame_ticket t={
            .frame=space.page_tables.tables[i],
            .generation=space.page_tables.table_generation[i],
            .role=NOVA_FRAME_PAGE_TABLE
        };
        assert(nova_pmm_ticket_valid(&t));
    }
    struct nova_pt_ops stack_only=ticket_ops();
    assert(!nova_user_space_frames_releasable(&space,&stack_only));
    assert(nova_user_space_frames_releasable(&space,&guarded));
    nova_user_space_destroy(&space,&guarded);
    assert(nova_pmm.in_use==image->page_count);
    assert(nova_process_drop(&processes,pid,free_image,phys_image,NULL));
    assert(nova_pmm.in_use==0);
}
static void test_stale_table_ticket_blocks_all_cleanup(void){
    reset_pmm();
    table_ticket_budget=UINT32_MAX;
    ticket_budget=UINT32_MAX;
    struct nova_process_table processes={0};
    uint32_t pid=stage_ticket_fixture(&processes);
    struct nova_process_image *image=nova_process_find(&processes,pid);
    struct nova_pt_ops guarded=fully_guarded_ops();
    struct nova_user_space space={0};
    assert(nova_user_space_prepare(&space,&guarded,image)==NOVA_PT_OK);
    struct nova_user_space original=space;
    struct nova_frame_ticket stale={
        .frame=space.page_tables.tables[0],
        .generation=space.page_tables.table_generation[0],
        .role=NOVA_FRAME_PAGE_TABLE
    };
    assert(nova_page_free_ticket(&stale));
    struct nova_frame_ticket replacement={0};
    assert(nova_page_alloc_ticket(NOVA_FRAME_PAGE_TABLE,&replacement));
    assert(replacement.frame==stale.frame);
    assert(replacement.generation!=stale.generation);
    memset(replacement.frame,0x6D,4096);
    const uint32_t before=nova_pmm.in_use;
    assert(!nova_user_space_frames_releasable(&space,&guarded));
    nova_user_space_destroy(&space,&guarded);
    assert(nova_pmm.in_use==before);
    assert(nova_pmm_ticket_valid(&replacement));
    for(unsigned i=0;i<4096;i++)
        assert(((uint8_t *)replacement.frame)[i]==0x6D);
    /* Only this test fixture knows the remaining valid owners. */
    for(uint16_t i=1;i<original.page_tables.tables_used;i++)
        assert(ticket_table_free(NULL,original.page_tables.tables[i],
                                 original.page_tables.table_generation[i]));
    for(uint32_t i=0;i<original.stack_count;i++)
        assert(ticket_stack_free(NULL,original.stack_frames[i],
                                 original.stack_generation[i]));
    assert(nova_page_free_ticket(&replacement));
    assert(nova_process_drop(&processes,pid,free_image,phys_image,NULL));
    assert(nova_pmm.in_use==0);
}
static void test_corrupt_table_ticket_and_rollback(void){
    reset_pmm();
    table_ticket_budget=UINT32_MAX;
    ticket_budget=UINT32_MAX;
    struct nova_process_table processes={0};
    uint32_t pid=stage_ticket_fixture(&processes);
    struct nova_process_image *image=nova_process_find(&processes,pid);
    struct nova_pt_ops guarded=fully_guarded_ops();
    struct nova_user_space space={0};
    assert(nova_user_space_prepare(&space,&guarded,image)==NOVA_PT_OK);
    struct nova_user_space original=space;
    const uint32_t before=nova_pmm.in_use;
    space.page_tables.table_generation[1]++;
    assert(!nova_user_space_frames_releasable(&space,&guarded));
    nova_user_space_destroy(&space,&guarded);
    assert(nova_pmm.in_use==before);
    space=original;
    nova_user_space_destroy(&space,&guarded);
    assert(nova_pmm.in_use==image->page_count);
    table_ticket_budget=2;
    assert(nova_user_space_prepare(&space,&guarded,image)==NOVA_PT_OUT_OF_MEMORY);
    assert(nova_pmm.in_use==image->page_count);
    table_ticket_budget=UINT32_MAX;
    assert(nova_process_drop(&processes,pid,free_image,phys_image,NULL));
    assert(nova_pmm.in_use==0);
}
static bool forge_table_alloc(void *ctx,void **frame,uint64_t *generation){
    if(!ticket_table_alloc(ctx,frame,generation))return false;
    forged_table_ticket=(struct nova_frame_ticket){
        .frame=*frame,.generation=*generation,.role=NOVA_FRAME_PAGE_TABLE
    };
    memset(*frame,0xA9,4096);
    (*generation)++;
    return true;
}
static void test_forged_table_ticket_never_zeroed(void){
    reset_pmm();
    table_ticket_budget=UINT32_MAX;
    ticket_budget=UINT32_MAX;
    forged_table_ticket=(struct nova_frame_ticket){0};
    struct nova_process_table processes={0};
    uint32_t pid=stage_ticket_fixture(&processes);
    struct nova_process_image *image=nova_process_find(&processes,pid);
    struct nova_pt_ops guarded=fully_guarded_ops();
    guarded.table_ticket_allocate=forge_table_alloc;
    struct nova_user_space space={0};
    assert(nova_user_space_prepare(&space,&guarded,image)==NOVA_PT_BAD_FRAME);
    assert(!space.page_tables.tables_used&&!space.stack_count&&!space.prepared);
    assert(nova_pmm_ticket_valid(&forged_table_ticket));
    for(unsigned i=0;i<4096;i++)
        assert(((uint8_t *)forged_table_ticket.frame)[i]==0xA9);
    assert(nova_pmm.in_use==image->page_count+1u);
    assert(nova_page_free_ticket(&forged_table_ticket));
    assert(nova_process_drop(&processes,pid,free_image,phys_image,NULL));
    assert(nova_pmm.in_use==0);
}
int main(void){
    test_role_transitions_and_corruption();
    test_offline_role_integration();
    test_guarded_stack_lifecycle();
    test_stale_stack_ticket_blocks_all_cleanup();
    test_corrupt_stack_ticket_and_rollback();
    test_forged_stack_ticket_never_zeroed();
    test_guarded_table_lifecycle();
    test_stale_table_ticket_blocks_all_cleanup();
    test_corrupt_table_ticket_and_rollback();
    test_forged_table_ticket_never_zeroed();
    puts("PMM roles and stack/table generation tickets: ABA, corruption, forged allocation, rollback and legacy checks OK.");
    return 0;
}
