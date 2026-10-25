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
    fail_second_read=false;budget=UINT32_MAX;
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
static bool free_owned(void *ctx,uint64_t owner,void *frame,uint64_t gen){
    (void)ctx;
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
static const struct nova_pt_ops ops={
    .allocate=table_alloc,.release=table_free,.physical_address=legacy_phys,
    .image_ticket_physical_address=phys_owned,
    .table_physical_address=table_phys
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
    assert(nova_pmm.in_use==4);
    assert(nova_pt_prepare(&tree,&ops,ia)==NOVA_PT_OK);
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
    intact(replacement.frame,0xC7);
    assert(!nova_owner_retire(owner)); /* leaked second frame remains owned */
    assert(nova_page_free_ticket(&replacement));
}
int main(void){
    test_separation_and_offline_preflight();
    test_failure_rollback();
    test_stale_owned_ticket();
    puts("Owned offline images: separate owners, guarded paging preflight, rollback, stale tickets OK.");
    return 0;
}
