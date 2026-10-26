/* Owner-bound staged ELF pages and offline PML4 preflight.
 * Host-only synthetic RAM: never executes ELF, touches disk or builds ISO.
 */
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/pmm.h"
#include "../kernel/user_space.h"
#define FRAMES 32u
#define ELF_BYTES 16384u
static _Alignas(4096) uint8_t ram[FRAMES][4096];
static uint8_t elf[ELF_BYTES];
static bool fail_second_read;
static unsigned budget;
static unsigned table_budget;
static int fail_image_free_after=-1;
static int fail_table_free_after=-1;
static int fail_stack_free_after=-1;
static struct nova_frame_ticket forged_table;
static struct nova_frame_ticket forged_image;
static void w16(size_t i,uint16_t x){elf[i]=(uint8_t)x;elf[i+1]=(uint8_t)(x>>8);}
static void w32(size_t i,uint32_t x){
    for(unsigned k=0;k<4;k++)elf[i+k]=(uint8_t)(x>>(8u*k));
}
static void w64(size_t i,uint64_t x){
    for(unsigned k=0;k<8;k++)elf[i+k]=(uint8_t)(x>>(8u*k));
}
static void ph(unsigned n,uint32_t flags,uint64_t off,uint64_t va){
    size_t i=64u+n*56u;
    w32(i,1);w32(i+4,flags);w64(i+8,off);w64(i+16,va);
    w64(i+32,128);w64(i+40,4096);w64(i+48,4096);
}
static void setup(void){
    nova_pmm=(struct nova_page_manager){0};
    memset(ram,0xA5,sizeof(ram));
    struct limine_memmap_entry region={
        .base=0x200000u,.length=FRAMES*4096u,.type=LIMINE_MEMMAP_USABLE};
    struct limine_memmap_entry *entries[]={&region};
    struct limine_memmap_response map={.entry_count=1,.entries=entries};
    struct limine_hhdm_response hhdm={0};
    assert((uintptr_t)ram>=0x200000u);
    hhdm.offset=(uint64_t)((uintptr_t)ram-0x200000u);
    assert(nova_pmm_init(&map,&hhdm));
    assert(nova_pmm_self_test());
    memset(elf,0,sizeof(elf));
    elf[0]=0x7f;elf[1]='E';elf[2]='L';elf[3]='F';
    elf[4]=2;elf[5]=1;elf[6]=1;
    w16(16,2);w16(18,62);w32(20,1);w64(24,0x401000);w64(32,64);
    w16(52,64);w16(54,56);w16(56,2);
    ph(0,5,4096,0x401000);ph(1,6,8192,0x402000);
    for(unsigned i=0;i<128;i++){
        elf[4096+i]=(uint8_t)(i+1u);
        elf[8192+i]=(uint8_t)(0x80u+i);
    }
    fail_second_read=false;budget=UINT32_MAX;table_budget=UINT32_MAX;
    fail_image_free_after=-1;fail_table_free_after=-1;fail_stack_free_after=-1;
    forged_table=(struct nova_frame_ticket){0};
}
static bool read_elf(void *ctx,uint64_t off,uint8_t *out,size_t n){
    (void)ctx;
    if(fail_second_read && off>=8192)return false;
    if(off>ELF_BYTES||n>ELF_BYTES-off)return false;
    memcpy(out,elf+(size_t)off,n);return true;
}
static bool alloc_owned(void *ctx,uint64_t owner,void **frame,uint64_t *gen){
    (void)ctx;
    if(!frame||!gen||budget==0)return false;
    struct nova_frame_ticket t={0};
    if(!nova_page_alloc_owned_ticket(NOVA_FRAME_IMAGE,owner,&t))return false;
    if(budget!=UINT32_MAX)budget--;
    *frame=t.frame;*gen=t.generation;return true;
}
static bool phys_owned(void *ctx,uint64_t owner,const void *frame,
                       uint64_t gen,uint64_t *out){
    (void)ctx;
    struct nova_frame_ticket t={
        .frame=(void*)frame,.generation=gen,.role=NOVA_FRAME_IMAGE,.owner=owner};
    return nova_pmm_physical_ticket(&t,out);
}
static bool forged_owned_image_alloc(void *ctx,uint64_t owner,void **frame,
                                     uint64_t *generation){
    if(!alloc_owned(ctx,owner,frame,generation))return false;
    forged_image=(struct nova_frame_ticket){
        .frame=*frame,.generation=*generation,
        .role=NOVA_FRAME_IMAGE,.owner=owner};
    memset(*frame,0xA7,4096);
    (*generation)++;
    return true;
}
static bool free_owned(void *ctx,uint64_t owner,void *frame,uint64_t gen){
    (void)ctx;
    if(fail_image_free_after==0){fail_image_free_after=-1;return false;}
    if(fail_image_free_after>0)fail_image_free_after--;
    struct nova_frame_ticket t={
        .frame=frame,.generation=gen,.role=NOVA_FRAME_IMAGE,.owner=owner};
    return nova_page_free_ticket(&t);
}
static bool legacy_phys(void *ctx,const void *frame,uint64_t *out){
    (void)ctx;return nova_pmm_physical_role(frame,NOVA_FRAME_IMAGE,out);
}
static const struct nova_process_guard_ops guard={
    .allocate_owned=alloc_owned,.physical_owned=phys_owned,
    .release_owned=free_owned};
static enum nova_process_result stage(struct nova_process_table *table,
                                      uint64_t owner,uint32_t *pid){
    return nova_process_stage_owned_guarded(table,"RAM:/TEST.ELF",
        read_elf,NULL,ELF_BYTES,legacy_phys,&guard,owner,NULL,pid);
}
static void intact(const uint8_t *p,uint8_t byte){
    for(unsigned i=0;i<4096;i++)assert(p[i]==byte);
}
static void *table_alloc(void *ctx){
    (void)ctx;return nova_page_alloc_role(NOVA_FRAME_PAGE_TABLE);
}
static void table_free(void *ctx,void *p){
    (void)ctx;assert(nova_page_free_role(p,NOVA_FRAME_PAGE_TABLE));
}
static bool table_phys(void *ctx,const void *p,uint64_t *out){
    (void)ctx;return nova_pmm_physical_role(p,NOVA_FRAME_PAGE_TABLE,out);
}
static bool owned_table_alloc(void *ctx,uint64_t owner,void **frame,
                              uint64_t *generation){
    (void)ctx;
    if(!frame||!generation||table_budget==0)return false;
    struct nova_frame_ticket ticket={0};
    if(!nova_page_alloc_owned_ticket(NOVA_FRAME_PAGE_TABLE,owner,&ticket))
        return false;
    if(table_budget!=UINT32_MAX)table_budget--;
    *frame=ticket.frame;*generation=ticket.generation;
    return true;
}
static bool owned_table_phys(void *ctx,uint64_t owner,const void *frame,
                             uint64_t generation,uint64_t *out){
    (void)ctx;
    struct nova_frame_ticket ticket={
        .frame=(void *)frame,.generation=generation,
        .role=NOVA_FRAME_PAGE_TABLE,.owner=owner};
    return nova_pmm_physical_ticket(&ticket,out);
}
static bool owned_table_free(void *ctx,uint64_t owner,void *frame,
                             uint64_t generation){
    (void)ctx;
    if(fail_table_free_after==0){fail_table_free_after=-1;return false;}
    if(fail_table_free_after>0)fail_table_free_after--;
    struct nova_frame_ticket ticket={
        .frame=frame,.generation=generation,
        .role=NOVA_FRAME_PAGE_TABLE,.owner=owner};
    return nova_page_free_ticket(&ticket);
}
static bool forged_owned_table_alloc(void *ctx,uint64_t owner,void **frame,
                                     uint64_t *generation){
    if(!owned_table_alloc(ctx,owner,frame,generation))return false;
    forged_table=(struct nova_frame_ticket){
        .frame=*frame,.generation=*generation,
        .role=NOVA_FRAME_PAGE_TABLE,.owner=owner};
    memset(*frame,0xA7,4096);
    (*generation)++;
    return true;
}
static unsigned stack_budget=UINT32_MAX;
static struct nova_frame_ticket forged_stack;
static bool owned_stack_alloc(void *ctx,uint64_t owner,void **frame,
                              uint64_t *generation){
    (void)ctx;
    if(!frame||!generation||stack_budget==0)return false;
    struct nova_frame_ticket ticket={0};
    if(!nova_page_alloc_owned_ticket(NOVA_FRAME_STACK,owner,&ticket))return false;
    if(stack_budget!=UINT32_MAX)stack_budget--;
    *frame=ticket.frame;*generation=ticket.generation;return true;
}
static bool owned_stack_phys(void *ctx,uint64_t owner,const void *frame,
                             uint64_t generation,uint64_t *out){
    (void)ctx;
    struct nova_frame_ticket ticket={
        .frame=(void *)frame,.generation=generation,
        .role=NOVA_FRAME_STACK,.owner=owner};
    return nova_pmm_physical_ticket(&ticket,out);
}
static bool owned_stack_free(void *ctx,uint64_t owner,void *frame,
                             uint64_t generation){
    (void)ctx;
    if(fail_stack_free_after==0){fail_stack_free_after=-1;return false;}
    if(fail_stack_free_after>0)fail_stack_free_after--;
    struct nova_frame_ticket ticket={
        .frame=frame,.generation=generation,
        .role=NOVA_FRAME_STACK,.owner=owner};
    return nova_page_free_ticket(&ticket);
}
static bool forged_owned_stack_alloc(void *ctx,uint64_t owner,void **frame,
                                     uint64_t *generation){
    if(!owned_stack_alloc(ctx,owner,frame,generation))return false;
    forged_stack=(struct nova_frame_ticket){
        .frame=*frame,.generation=*generation,
        .role=NOVA_FRAME_STACK,.owner=owner};
    memset(*frame,0xA7,4096);
    (*generation)++;
    return true;
}
static const struct nova_pt_ops ops={
    .allocate=table_alloc,.release=table_free,.physical_address=legacy_phys,
    .image_ticket_physical_address=phys_owned,
    .table_physical_address=table_phys,
    .table_owned_ticket_allocate=owned_table_alloc,
    .table_owned_ticket_physical_address=owned_table_phys,
    .table_owned_ticket_release=owned_table_free,
    .stack_owned_ticket_allocate=owned_stack_alloc,
    .stack_owned_ticket_physical_address=owned_stack_phys,
    .stack_owned_ticket_release=owned_stack_free
};
static void test_separation_and_offline_preflight(void){
    setup();
    uint64_t a=0,b=0;
    assert(nova_owner_create(&a)&&nova_owner_create(&b));
    struct nova_process_table table={0};
    uint32_t pid_a=0,pid_b=0;
    assert(stage(&table,a,&pid_a)==NOVA_PROCESS_OK);
    assert(stage(&table,b,&pid_b)==NOVA_PROCESS_OK);
    struct nova_process_image *ia=nova_process_find(&table,pid_a);
    struct nova_process_image *ib=nova_process_find(&table,pid_b);
    assert(ia&&ib&&ia->owner==a&&ib->owner==b&&ia->page_count==2);
    struct nova_frame_ticket original={
        .frame=ia->pages[0].frame,.generation=ia->pages[0].generation,
        .role=NOVA_FRAME_IMAGE,.owner=a};
    assert(nova_pmm_ticket_valid(&original));
    struct nova_frame_ticket forged=original;forged.owner=b;
    assert(!nova_pmm_ticket_valid(&forged));
    assert(!nova_page_free_ticket(&forged));
    assert(!nova_page_free_role(original.frame,NOVA_FRAME_IMAGE));
    assert(!nova_owner_retire(a)&&!nova_owner_retire(b));
    struct nova_page_table tree={0};
    struct nova_pt_ops missing=ops;missing.image_ticket_physical_address=NULL;
    assert(nova_pt_prepare(&tree,&missing,ia)==NOVA_PT_BAD_FRAME);
    struct nova_pt_ops missing_owner=ops;
    missing_owner.table_owned_ticket_release=NULL;
    assert(nova_pt_prepare(&tree,&missing_owner,ia)==NOVA_PT_INVALID);
    missing_owner=ops;
    missing_owner.table_owned_ticket_allocate=NULL;
    missing_owner.table_owned_ticket_physical_address=NULL;
    missing_owner.table_owned_ticket_release=NULL;
    assert(nova_pt_prepare(&tree,&missing_owner,ia)==NOVA_PT_INVALID);
    assert(nova_pmm.in_use==4);
    assert(nova_pt_prepare(&tree,&ops,ia)==NOVA_PT_OK);
    assert(tree.owner==a && tree.tables_guarded);
    for(uint16_t i=0;i<tree.tables_used;i++){
        struct nova_frame_ticket ticket={
            .frame=tree.tables[i],.generation=tree.table_generation[i],
            .role=NOVA_FRAME_PAGE_TABLE,.owner=a};
        assert(nova_pmm_ticket_valid(&ticket));
        assert(!nova_page_free_role(ticket.frame,NOVA_FRAME_PAGE_TABLE));
        ticket.owner=b;
        assert(!nova_page_free_ticket(&ticket));
    }
    tree.owner=b;
    assert(!nova_pt_tables_releasable(&tree,&ops));
    tree.owner=a;
    assert(nova_pt_tables_releasable(&tree,&ops));
    uint64_t phys=0,flags=0;
    assert(nova_pt_lookup(&tree,ia->pages[0].virtual_page,&phys,&flags));
    assert(phys==ia->pages[0].physical_page);
    nova_pt_destroy(&tree,&ops);
    assert(nova_pmm.in_use==4);
    uint64_t original_owner=ia->owner;
    ia->owner=b; /* forged owner cannot authorize a foreign drop */
    assert(!nova_process_frames_releasable_guarded(
        ia,&table,legacy_phys,NULL,&guard));
    ia->owner=original_owner;
    memset(ib->pages[0].frame,0xD2,4096);
    assert(nova_process_drop_guarded(&table,pid_a,legacy_phys,&guard,NULL));
    assert(nova_owner_retire(a));
    assert(!nova_owner_retire(b));
    intact(ib->pages[0].frame,0xD2);
    assert(nova_process_drop_guarded(&table,pid_b,legacy_phys,&guard,NULL));
    assert(nova_owner_retire(b));
    assert(nova_pmm.in_use==0&&nova_pmm_metadata_valid());
}
static void test_failure_rollback(void){
    setup();uint64_t owner=0;assert(nova_owner_create(&owner));
    struct nova_process_table table={0};
    uint32_t pid=99;
    assert(stage(&table,0,&pid)==NOVA_PROCESS_BAD_INPUT&&pid==0);
    fail_second_read=true;
    assert(stage(&table,owner,&pid)==NOVA_PROCESS_READ_ERROR);
    assert(pid==0&&nova_pmm.in_use==0&&nova_owner_retire(owner));
    setup();assert(nova_owner_create(&owner));
    table=(struct nova_process_table){0};budget=1;pid=99;
    assert(stage(&table,owner,&pid)==NOVA_PROCESS_NO_MEMORY);
    assert(pid==0&&nova_pmm.in_use==0&&nova_owner_retire(owner));
}
static void test_stale_owned_ticket(void){
    setup();uint64_t owner=0;assert(nova_owner_create(&owner));
    struct nova_process_table table={0};uint32_t pid=0;
    assert(stage(&table,owner,&pid)==NOVA_PROCESS_OK);
    struct nova_process_image *im=nova_process_find(&table,pid);
    struct nova_frame_ticket old={
        .frame=im->pages[0].frame,.generation=im->pages[0].generation,
        .role=NOVA_FRAME_IMAGE,.owner=owner};
    assert(nova_page_free_ticket(&old));
    struct nova_frame_ticket replacement={0};
    assert(nova_page_alloc_owned_ticket(NOVA_FRAME_IMAGE,owner,&replacement));
    assert(replacement.frame==old.frame);
    memset(replacement.frame,0xC7,4096);
    assert(!nova_process_drop_guarded(&table,pid,legacy_phys,&guard,NULL));
    assert(im->staged&&im->page_count==2); /* failed preflight retains tickets */
    intact(replacement.frame,0xC7);
    assert(!nova_owner_retire(owner));
    assert(nova_page_free_ticket(&replacement));
    assert(free_owned(NULL,owner,im->pages[1].frame,im->pages[1].generation));
    *im=(struct nova_process_image){0}; /* page 0 was freed/reused above */
    assert(nova_owner_retire(owner));
    assert(nova_pmm.in_use==0&&nova_pmm_metadata_valid());
}
static void test_owned_table_rollback_and_forgery(void){
    setup();uint64_t owner=0;assert(nova_owner_create(&owner));
    struct nova_process_table table={0};uint32_t pid=0;
    assert(stage(&table,owner,&pid)==NOVA_PROCESS_OK);
    struct nova_process_image *image=nova_process_find(&table,pid);
    assert(image);
    struct nova_page_table tree={0};
    table_budget=2;
    assert(nova_pt_prepare(&tree,&ops,image)==NOVA_PT_OUT_OF_MEMORY);
    assert(nova_pmm.in_use==image->page_count);
    assert(!nova_owner_retire(owner));
    table_budget=UINT32_MAX;
    struct nova_pt_ops forged=ops;
    forged.table_owned_ticket_allocate=forged_owned_table_alloc;
    assert(nova_pt_prepare(&tree,&forged,image)==NOVA_PT_BAD_FRAME);
    assert(tree.tables_used==0 && !tree.prepared);
    assert(nova_pmm_ticket_valid(&forged_table));
    intact(forged_table.frame,0xA7);
    assert(nova_page_free_ticket(&forged_table));
    assert(nova_process_drop_guarded(&table,pid,legacy_phys,&guard,NULL));
    assert(nova_owner_retire(owner));
    assert(nova_pmm.in_use==0 && nova_pmm_metadata_valid());
}
static void test_stale_owned_table_ticket(void){
    setup();uint64_t owner=0;assert(nova_owner_create(&owner));
    struct nova_process_table table={0};uint32_t pid=0;
    assert(stage(&table,owner,&pid)==NOVA_PROCESS_OK);
    struct nova_process_image *image=nova_process_find(&table,pid);
    assert(image);
    struct nova_page_table tree={0};
    assert(nova_pt_prepare(&tree,&ops,image)==NOVA_PT_OK);
    struct nova_page_table saved=tree;
    struct nova_frame_ticket old={
        .frame=tree.tables[0],.generation=tree.table_generation[0],
        .role=NOVA_FRAME_PAGE_TABLE,.owner=owner};
    assert(nova_page_free_ticket(&old));
    struct nova_frame_ticket replacement={0};
    assert(nova_page_alloc_owned_ticket(NOVA_FRAME_PAGE_TABLE,owner,&replacement));
    assert(replacement.frame==old.frame);
    assert(replacement.generation!=old.generation);
    memset(replacement.frame,0xC7,4096);
    const uint32_t before=nova_pmm.in_use;
    assert(!nova_pt_tables_releasable(&tree,&ops));
    assert(!nova_pt_destroy(&tree,&ops));
    assert(!tree.teardown_started&&tree.prepared&&
           tree.tables_used==saved.tables_used);
    assert(nova_pmm.in_use==before);
    intact(replacement.frame,0xC7);
    assert(!nova_owner_retire(owner));
    for(uint16_t i=1;i<saved.tables_used;i++)
        assert(owned_table_free(NULL,owner,saved.tables[i],
                                saved.table_generation[i]));
    assert(nova_page_free_ticket(&replacement));
    assert(nova_process_drop_guarded(&table,pid,legacy_phys,&guard,NULL));
    assert(nova_owner_retire(owner));
    assert(nova_pmm.in_use==0 && nova_pmm_metadata_valid());
}
static void test_owned_stack_lifecycle(void){
    setup();stack_budget=UINT32_MAX;
    uint64_t a=0,b=0;
    assert(nova_owner_create(&a)&&nova_owner_create(&b));
    struct nova_process_table table={0};
    uint32_t pa=0,pb=0;
    assert(stage(&table,a,&pa)==NOVA_PROCESS_OK);
    assert(stage(&table,b,&pb)==NOVA_PROCESS_OK);
    struct nova_process_image *ia=nova_process_find(&table,pa);
    struct nova_process_image *ib=nova_process_find(&table,pb);
    assert(ia&&ib);
    struct nova_user_space sa={0},sb={0};
    struct nova_pt_ops missing=ops;
    missing.stack_owned_ticket_release=NULL;
    assert(nova_user_space_prepare(&sa,&missing,ia)==NOVA_PT_INVALID);
    assert(sa.stack_count==0 && !sa.prepared);
    assert(nova_user_space_prepare(&sa,&ops,ia)==NOVA_PT_OK);
    assert(nova_user_space_prepare(&sb,&ops,ib)==NOVA_PT_OK);
    assert(nova_user_stack_guard_is_unmapped(&sa));
    assert(nova_user_space_frames_releasable(&sa,&ops));
    for(uint32_t i=0;i<sa.stack_count;i++){
        struct nova_frame_ticket ticket={
            .frame=sa.stack_frames[i],.generation=sa.stack_generation[i],
            .role=NOVA_FRAME_STACK,.owner=a};
        assert(nova_pmm_ticket_valid(&ticket));
        assert(!nova_page_free_role(ticket.frame,NOVA_FRAME_STACK));
        ticket.owner=b;
        assert(!nova_pmm_ticket_valid(&ticket));
        assert(!nova_page_free_ticket(&ticket));
    }
    sa.stack_generation[0]++;
    assert(!nova_user_space_frames_releasable(&sa,&ops));
    sa.stack_generation[0]--;
    sa.page_tables.owner=b;
    assert(!nova_user_space_frames_releasable(&sa,&ops));
    sa.page_tables.owner=a;
    assert(nova_user_space_frames_releasable(&sa,&ops));
    memset(sb.stack_frames[0],0xC7,4096);
    nova_user_space_destroy(&sa,&ops);
    assert(nova_process_drop_guarded(&table,pa,legacy_phys,&guard,NULL));
    assert(nova_owner_retire(a));
    intact(sb.stack_frames[0],0xC7);
    nova_user_space_destroy(&sb,&ops);
    assert(nova_process_drop_guarded(&table,pb,legacy_phys,&guard,NULL));
    assert(nova_owner_retire(b));
    assert(nova_pmm.in_use==0 && nova_pmm_metadata_valid());
}
static void test_owned_stack_rollback_and_forgery(void){
    setup();stack_budget=UINT32_MAX;
    uint64_t owner=0;assert(nova_owner_create(&owner));
    struct nova_process_table table={0};uint32_t pid=0;
    assert(stage(&table,owner,&pid)==NOVA_PROCESS_OK);
    struct nova_process_image *image=nova_process_find(&table,pid);
    assert(image);
    struct nova_user_space space={0};
    stack_budget=2;
    assert(nova_user_space_prepare(&space,&ops,image)==NOVA_PT_OUT_OF_MEMORY);
    assert(nova_pmm.in_use==image->page_count);
    stack_budget=UINT32_MAX;
    struct nova_pt_ops forged=ops;
    forged.stack_owned_ticket_allocate=forged_owned_stack_alloc;
    assert(nova_user_space_prepare(&space,&forged,image)==NOVA_PT_BAD_FRAME);
    assert(!space.prepared && space.stack_count==0);
    assert(nova_pmm_ticket_valid(&forged_stack));
    intact(forged_stack.frame,0xA7);
    assert(nova_page_free_ticket(&forged_stack));
    assert(nova_process_drop_guarded(&table,pid,legacy_phys,&guard,NULL));
    assert(nova_owner_retire(owner));
    assert(nova_pmm.in_use==0 && nova_pmm_metadata_valid());
}
static void test_stale_owned_stack_ticket(void){
    setup();stack_budget=UINT32_MAX;
    uint64_t owner=0;assert(nova_owner_create(&owner));
    struct nova_process_table table={0};uint32_t pid=0;
    assert(stage(&table,owner,&pid)==NOVA_PROCESS_OK);
    struct nova_process_image *image=nova_process_find(&table,pid);
    assert(image);
    struct nova_user_space space={0};
    assert(nova_user_space_prepare(&space,&ops,image)==NOVA_PT_OK);
    struct nova_user_space saved=space;
    struct nova_frame_ticket old={
        .frame=space.stack_frames[0],.generation=space.stack_generation[0],
        .role=NOVA_FRAME_STACK,.owner=owner};
    assert(nova_page_free_ticket(&old));
    struct nova_frame_ticket replacement={0};
    assert(nova_page_alloc_owned_ticket(NOVA_FRAME_STACK,owner,&replacement));
    assert(replacement.frame==old.frame && replacement.generation!=old.generation);
    memset(replacement.frame,0xC7,4096);
    const uint32_t before=nova_pmm.in_use;
    assert(!nova_user_space_frames_releasable(&space,&ops));
    assert(!nova_user_space_destroy(&space,&ops));
    assert(space.prepared&&!space.teardown_started);
    assert(nova_pmm.in_use==before);
    intact(replacement.frame,0xC7);
    for(uint16_t i=0;i<saved.page_tables.tables_used;i++)
        assert(owned_table_free(NULL,owner,saved.page_tables.tables[i],
                                saved.page_tables.table_generation[i]));
    for(uint32_t i=1;i<saved.stack_count;i++)
        assert(owned_stack_free(NULL,owner,saved.stack_frames[i],
                                saved.stack_generation[i]));
    assert(nova_page_free_ticket(&replacement));
    assert(nova_process_drop_guarded(&table,pid,legacy_phys,&guard,NULL));
    assert(nova_owner_retire(owner));
    assert(nova_pmm.in_use==0 && nova_pmm_metadata_valid());
}

/* Corrupted cross-descriptor identity must block teardown without side effects. */
static void test_offline_drop_preflight(void){
    setup();stack_budget=UINT32_MAX;
    uint64_t a=0,b=0;
    assert(nova_owner_create(&a)&&nova_owner_create(&b));
    struct nova_process_table table={0};
    uint32_t pa=0,pb=0;
    assert(stage(&table,a,&pa)==NOVA_PROCESS_OK);
    assert(stage(&table,b,&pb)==NOVA_PROCESS_OK);
    struct nova_process_image *ia=nova_process_find(&table,pa);
    struct nova_process_image *ib=nova_process_find(&table,pb);
    assert(ia&&ib);
    struct nova_user_space sa={0},sb={0};
    assert(nova_user_space_prepare(&sa,&ops,ia)==NOVA_PT_OK);
    assert(nova_user_space_prepare(&sb,&ops,ib)==NOVA_PT_OK);
    const uint32_t baseline=nova_pmm.in_use;
    assert(nova_offline_drop_preflight(ia,&table,&sa,&ops,
                                       legacy_phys,&guard,NULL));
    sa.stack_generation[0]++;
    assert(!nova_offline_drop_preflight(ia,&table,&sa,&ops,
                                        legacy_phys,&guard,NULL));
    sa.stack_generation[0]--;
    sa.page_tables.protected_physical[0]^=NOVA_PT_PAGE_SIZE;
    assert(!nova_offline_drop_preflight(ia,&table,&sa,&ops,
                                        legacy_phys,&guard,NULL));
    sa.page_tables.protected_physical[0]^=NOVA_PT_PAGE_SIZE;
    sa.page_tables.owner=b;
    assert(!nova_offline_drop_preflight(ia,&table,&sa,&ops,
                                        legacy_phys,&guard,NULL));
    sa.page_tables.owner=a;
    sa.page_tables.mapped_entry[0]^=NOVA_PT_RW;
    assert(!nova_offline_drop_preflight(ia,&table,&sa,&ops,
                                        legacy_phys,&guard,NULL));
    sa.page_tables.mapped_entry[0]^=NOVA_PT_RW;
    ia->pages[0].generation++;
    assert(!nova_offline_drop_preflight(ia,&table,&sa,&ops,
                                        legacy_phys,&guard,NULL));
    ia->pages[0].generation--;
    assert(nova_pmm.in_use==baseline && ia->staged && sa.prepared);
    assert(nova_offline_drop_preflight(ia,&table,&sa,&ops,
                                       legacy_phys,&guard,NULL));
    memset(sb.stack_frames[0],0xC7,4096);
    nova_user_space_destroy(&sa,&ops);
    assert(nova_process_drop_guarded(&table,pa,legacy_phys,&guard,NULL));
    assert(nova_owner_retire(a));
    intact(sb.stack_frames[0],0xC7);
    assert(nova_offline_drop_preflight(ib,&table,&sb,&ops,
                                       legacy_phys,&guard,NULL));
    nova_user_space_destroy(&sb,&ops);
    assert(nova_process_drop_guarded(&table,pb,legacy_phys,&guard,NULL));
    assert(nova_owner_retire(b));
    assert(nova_pmm.in_use==0 && nova_pmm_metadata_valid());
}

/* Inject a callback failure after one successful release; remaining tickets
 * must stay reachable and a second teardown attempt must finish cleanup. */
static void test_retryable_image_release(void){
    setup();uint64_t owner=0;assert(nova_owner_create(&owner));
    struct nova_process_table table={0};uint32_t pid=0;
    assert(stage(&table,owner,&pid)==NOVA_PROCESS_OK);
    struct nova_process_image *image=nova_process_find(&table,pid);
    assert(image&&image->page_count==2);
    uint8_t *remaining=image->pages[0].frame;
    struct nova_user_space empty={0};
    assert(nova_user_space_is_empty(&empty));
    assert(nova_offline_drop_preflight(image,&table,&empty,&ops,
                                       legacy_phys,&guard,NULL));
    const uint32_t before=nova_pmm.in_use;
    fail_image_free_after=1;
    assert(!nova_process_drop_guarded(&table,pid,legacy_phys,&guard,NULL));
    assert(image->staged&&image->page_count==1&&image->pages[0].frame==remaining);
    assert(nova_pmm.in_use==before-1u);
    assert(nova_offline_drop_preflight(image,&table,&empty,&ops,
                                       legacy_phys,&guard,NULL));
    assert(!nova_owner_retire(owner));
    assert(nova_process_drop_guarded(&table,pid,legacy_phys,&guard,NULL));
    assert(!nova_process_find(&table,pid));
    assert(nova_owner_retire(owner));
    assert(nova_pmm.in_use==0&&nova_pmm_metadata_valid());
}
static void test_retryable_space_teardown(void){
    setup();stack_budget=UINT32_MAX;
    uint64_t owner=0;assert(nova_owner_create(&owner));
    struct nova_process_table table={0};uint32_t pid=0;
    assert(stage(&table,owner,&pid)==NOVA_PROCESS_OK);
    struct nova_process_image *image=nova_process_find(&table,pid);
    assert(image);
    struct nova_user_space space={0};
    assert(nova_user_space_prepare(&space,&ops,image)==NOVA_PT_OK);
    const uint32_t before=nova_pmm.in_use;
    const uint16_t table_count=space.page_tables.tables_used;
    fail_stack_free_after=2;
    assert(!nova_user_space_destroy(&space,&ops));
    assert(space.teardown_started&&!space.prepared&&space.stack_count==2);
    assert(space.page_tables.teardown_started&&!space.page_tables.prepared);
    assert(space.page_tables.tables_used==table_count);
    assert(nova_pmm.in_use==before-2u);
    uint64_t probe_phys=0,probe_flags=0;
    assert(!nova_pt_lookup(&space.page_tables,image->pages[0].virtual_page,
                           &probe_phys,&probe_flags));
    assert(!nova_offline_drop_preflight(image,&table,&space,&ops,
                                        legacy_phys,&guard,NULL));
    fail_stack_free_after=-1;
    fail_table_free_after=1;
    assert(!nova_user_space_destroy(&space,&ops));
    assert(space.teardown_started&&space.stack_count==0);
    assert(space.page_tables.teardown_started&&!space.page_tables.prepared);
    assert(space.page_tables.tables_used==table_count-1u);
    assert(nova_pmm.in_use==before-5u);
    assert(!nova_offline_drop_preflight(image,&table,&space,&ops,
                                        legacy_phys,&guard,NULL));
    fail_table_free_after=-1;
    assert(nova_user_space_destroy(&space,&ops));
    assert(nova_user_space_is_empty(&space));
    assert(nova_pmm.in_use==image->page_count);
    assert(nova_offline_drop_preflight(image,&table,&space,&ops,
                                       legacy_phys,&guard,NULL));
    assert(nova_process_drop_guarded(&table,pid,legacy_phys,&guard,NULL));
    assert(nova_owner_retire(owner));
    assert(nova_pmm.in_use==0&&nova_pmm_metadata_valid());
}

static bool retire_owner_for_test(void *ctx,uint64_t owner){
    (void)ctx;
    return nova_owner_retire(owner);
}
/* A failed PSTAGE rollback must reserve its slot and preserve tickets
 * until a later retry can release frames and retire the original owner. */
static void test_retryable_stage_rollback(void){
    setup();
    uint64_t owner=0,other=0;
    assert(nova_owner_create(&owner)&&nova_owner_create(&other));
    struct nova_process_table table={0};
    uint32_t pid=99;
    fail_second_read=true;
    fail_image_free_after=0;
    assert(stage(&table,owner,&pid)==NOVA_PROCESS_ROLLBACK_PENDING);
    assert(pid==0&&nova_process_count(&table)==0);
    assert(table.slots[0].rollback_pending&&!table.slots[0].staged);
    assert(table.slots[0].owner==owner&&table.slots[0].page_count==2);
    assert(!nova_owner_retire(owner));
    table.slots[0].label[5]='X';
    assert(!nova_process_retry_stage_rollback(&table,0,NULL,legacy_phys,&guard,
                                              retire_owner_for_test,NULL));
    assert(nova_pmm.in_use==2);
    table.slots[0].label[5]=0;

    /* Malformed pending tickets quarantine the whole table before PSTAGE
     * performs ELF reads or allocates frames. No PID or PMM state may change. */
    const uint64_t saved_physical=table.slots[0].pages[0].physical_page;
    table.slots[0].pages[0].physical_page=0;
    uint32_t rejected_pid=123;
    assert(stage(&table,other,&rejected_pid)==NOVA_PROCESS_NO_SLOT);
    assert(rejected_pid==0&&table.next_pid==0&&nova_pmm.in_use==2);
    table.slots[0].pages[0].physical_page=saved_physical;

    const uint16_t saved_count=table.slots[0].page_count;
    table.slots[0].page_count=NOVA_PROCESS_MAX_PAGES+1u;
    rejected_pid=123;
    assert(stage(&table,other,&rejected_pid)==NOVA_PROCESS_NO_SLOT);
    assert(rejected_pid==0&&table.next_pid==0&&nova_pmm.in_use==2);
    table.slots[0].page_count=saved_count;

    table.slots[0].pages[NOVA_PROCESS_MAX_PAGES-1u].flags=1u;
    rejected_pid=123;
    assert(stage(&table,other,&rejected_pid)==NOVA_PROCESS_NO_SLOT);
    assert(rejected_pid==0&&table.next_pid==0&&nova_pmm.in_use==2);
    table.slots[0].pages[NOVA_PROCESS_MAX_PAGES-1u].flags=0;

    /* A new image may use another slot, but must not overwrite the pending
     * descriptor or alias any of its still-owned frames. */
    fail_second_read=false;
    uint32_t other_pid=0;
    assert(stage(&table,other,&other_pid)==NOVA_PROCESS_OK);
    assert(other_pid&&table.slots[0].rollback_pending);
    assert(nova_process_find(&table,other_pid)==&table.slots[1]);

    struct nova_frame_ticket orphan={0};
    assert(nova_page_alloc_owned_ticket(NOVA_FRAME_GENERAL,owner,&orphan));
    assert(!nova_process_retry_stage_rollback(&table,0,NULL,legacy_phys,&guard,
                                              retire_owner_for_test,NULL));
    assert(table.slots[0].rollback_pending&&!table.slots[0].staged);
    assert(table.slots[0].owner==owner&&table.slots[0].page_count==0);
    assert(nova_pmm_ticket_valid(&orphan));
    assert(nova_page_free_ticket(&orphan));
    assert(nova_process_retry_stage_rollback(&table,0,NULL,legacy_phys,&guard,
                                              retire_owner_for_test,NULL));
    assert(!table.slots[0].rollback_pending&&!table.slots[0].staged);
    assert(nova_process_drop_guarded(&table,other_pid,legacy_phys,&guard,NULL));
    assert(nova_owner_retire(other));
    assert(nova_pmm.in_use==0&&nova_pmm_metadata_valid());

    /* A forged allocation ticket is not freed, but its owner must not be
     * forgotten when the tracked-image rollback itself succeeds. */
    setup();
    assert(nova_owner_create(&owner));
    table=(struct nova_process_table){0};
    pid=0;
    const struct nova_process_guard_ops forged_guard={
        .allocate_owned=forged_owned_image_alloc,
        .physical_owned=phys_owned,.release_owned=free_owned};
    assert(nova_process_stage_owned_guarded(&table,"RAM:/TEST.ELF",
        read_elf,NULL,ELF_BYTES,legacy_phys,&forged_guard,owner,NULL,&pid)==
        NOVA_PROCESS_ROLLBACK_PENDING);
    assert(pid==0&&table.slots[0].rollback_pending&&
           table.slots[0].owner==owner&&table.slots[0].page_count==0);
    assert(nova_pmm_ticket_valid(&forged_image));
    intact(forged_image.frame,0xA7);
    assert(!nova_process_retry_stage_rollback(&table,0,NULL,legacy_phys,
        &forged_guard,retire_owner_for_test,NULL));
    intact(forged_image.frame,0xA7);
    assert(nova_page_free_ticket(&forged_image));
    assert(nova_process_retry_stage_rollback(&table,0,NULL,legacy_phys,
        &forged_guard,retire_owner_for_test,NULL));
    assert(!table.slots[0].rollback_pending);
    assert(nova_pmm.in_use==0&&nova_pmm_metadata_valid());
}
/* An inactive-looking slot may still contain the only saved ticket for
 * a live frame. PSTAGE must quarantine the table rather than erase it. */
static void test_untracked_slot_quarantine(void){
    setup();
    uint64_t owner=0;
    assert(nova_owner_create(&owner));
    struct nova_frame_ticket held={0};
    assert(nova_page_alloc_owned_ticket(NOVA_FRAME_IMAGE,owner,&held));
    memset(held.frame,0xD9,4096);
    uint64_t physical=0;
    assert(nova_pmm_physical_ticket(&held,&physical));

    struct nova_process_table table={0};
    table.slots[0].guarded=true;
    table.slots[0].owner=owner;
    table.slots[0].page_count=1;
    table.slots[0].pages[0]=(struct nova_process_page){
        .virtual_page=UINT64_C(0x401000),
        .frame=(uint8_t *)held.frame,
        .physical_page=physical,
        .generation=held.generation,
        .flags=5
    };
    const uint32_t in_use=nova_pmm.in_use;
    uint32_t pid=77;
    assert(stage(&table,owner,&pid)==NOVA_PROCESS_NO_SLOT);
    assert(pid==0&&table.next_pid==0);
    assert(!table.slots[0].staged&&!table.slots[0].rollback_pending);
    assert(table.slots[0].owner==owner&&table.slots[0].page_count==1);
    assert(table.slots[0].pages[0].frame==held.frame);
    assert(table.slots[0].pages[0].generation==held.generation);
    assert(nova_pmm.in_use==in_use&&nova_pmm_ticket_valid(&held));
    intact(held.frame,0xD9);
    assert(!nova_owner_retire(owner));

    /* Once the test explicitly resolves the orphan ticket and clears the
     * quarantined descriptor, normal staging can resume. */
    assert(nova_page_free_ticket(&held));
    table.slots[0]=(struct nova_process_image){0};
    assert(nova_owner_retire(owner));
    uint64_t next_owner=0;
    assert(nova_owner_create(&next_owner));
    assert(stage(&table,next_owner,&pid)==NOVA_PROCESS_OK);
    assert(nova_process_drop_guarded(&table,pid,legacy_phys,&guard,NULL));
    assert(nova_owner_retire(next_owner));
    assert(nova_pmm.in_use==0&&nova_pmm_metadata_valid());
}
static void test_retryable_owner_retirement(void){
    setup();
    uint64_t owner=0;
    assert(nova_owner_create(&owner));
    struct nova_process_table table={0};
    uint32_t pid=0;
    assert(stage(&table,owner,&pid)==NOVA_PROCESS_OK);
    assert(nova_process_count(&table)==1);
    struct nova_frame_ticket orphan={0};
    assert(nova_page_alloc_owned_ticket(NOVA_FRAME_GENERAL,owner,&orphan));
    assert(!nova_process_drop_owned_guarded(&table,pid,legacy_phys,&guard,
                                            retire_owner_for_test,NULL));
    struct nova_process_image *image=nova_process_find_any(&table,pid);
    assert(image&&image->owner_retirement_pending&&image->owner==owner);
    assert(image->staged&&image->guarded&&image->page_count==0);
    assert(nova_process_find(&table,pid)==NULL);
    assert(nova_process_count(&table)==0);
    assert(nova_pmm_ticket_valid(&orphan));
    assert(!nova_process_drop_guarded(&table,pid,legacy_phys,&guard,NULL));
    assert(!nova_process_frames_releasable_guarded(
        image,&table,legacy_phys,NULL,&guard));
    image->rollback_pending=true; /* conflicting teardown states are invalid */
    assert(!nova_process_drop_owned_guarded(&table,pid,legacy_phys,&guard,
                                            retire_owner_for_test,NULL));
    image->rollback_pending=false;
    image->entry=0x400000u; /* corrupt tombstone metadata must not retire */
    assert(!nova_process_drop_owned_guarded(&table,pid,legacy_phys,&guard,
                                            retire_owner_for_test,NULL));
    assert(image->owner_retirement_pending);
    image->entry=0;
    assert(nova_pmm.in_use==1&&!nova_owner_retire(owner));
    /* Once the unrelated owner frame is resolved, PDROP retries only owner
     * retirement; it must not attempt to free image frames a second time. */
    assert(nova_page_free_ticket(&orphan));
    assert(nova_process_drop_owned_guarded(&table,pid,legacy_phys,&guard,
                                           retire_owner_for_test,NULL));
    assert(nova_process_find_any(&table,pid)==NULL);
    assert(nova_process_count(&table)==0);
    assert(nova_pmm.in_use==0&&nova_pmm_metadata_valid());
}

int main(void){
    assert(strcmp(nova_elf_result_name(NOVA_ELF_OK),
                  "VALID STATIC ELF64 (NOT EXECUTED)")==0);
    test_separation_and_offline_preflight();
    test_failure_rollback();
    test_stale_owned_ticket();
    test_owned_table_rollback_and_forgery();
    test_stale_owned_table_ticket();
    test_owned_stack_lifecycle();
    test_owned_stack_rollback_and_forgery();
    test_stale_owned_stack_ticket();
    test_offline_drop_preflight();
    test_retryable_image_release();
    test_retryable_space_teardown();
    test_retryable_stage_rollback();
    test_untracked_slot_quarantine();
    test_retryable_owner_retirement();
    puts("Owned offline ELF and page tables: owner isolation, rollback, forged and stale tickets OK.");
    return 0;
}
