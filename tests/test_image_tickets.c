/* Generation-guarded staged ELF image tests with the REAL PMM ledger.
 * Host only: no CR3, user-mode execution, disk access, ISO or VM boot.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/pmm.h"
#include "../kernel/process_image.h"

#define FRAMES 16u
#define ELF_BYTES 16384u
static _Alignas(4096) uint8_t ram[FRAMES][4096];
static uint8_t elf[ELF_BYTES];
static bool deny_second_read=false;
static unsigned alloc_budget=UINT32_MAX;

static void w16(size_t at,uint16_t n){elf[at]=(uint8_t)n;elf[at+1]=(uint8_t)(n>>8);}
static void w32(size_t at,uint32_t n){
    for(unsigned i=0;i<4;i++)elf[at+i]=(uint8_t)(n>>(8u*i));
}
static void w64(size_t at,uint64_t n){
    for(unsigned i=0;i<8;i++)elf[at+i]=(uint8_t)(n>>(8u*i));
}
static void ph(unsigned i,uint32_t flags,uint64_t offset,uint64_t va){
    const size_t p=64u+i*56u;
    w32(p,1);w32(p+4,flags);w64(p+8,offset);w64(p+16,va);
    w64(p+32,128);w64(p+40,4096);w64(p+48,4096);
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
    ph(0,5,4096,0x401000);
    ph(1,6,8192,0x402000);
    for(unsigned i=0;i<128;i++){
        elf[4096+i]=(uint8_t)(i+1);
        elf[8192+i]=(uint8_t)(0x80+i);
    }
    deny_second_read=false;
    alloc_budget=UINT32_MAX;
}
static bool read_elf(void *ctx,uint64_t off,uint8_t *out,size_t n){
    (void)ctx;
    if(deny_second_read && off>=8192)return false;
    if(off>ELF_BYTES || n>ELF_BYTES-off)return false;
    memcpy(out,elf+(size_t)off,n);
    return true;
}
static bool image_alloc(void *ctx,void **out,uint64_t *generation){
    (void)ctx;
    if(!out||!generation||alloc_budget==0)return false;
    struct nova_frame_ticket ticket={0};
    if(!nova_page_alloc_ticket(NOVA_FRAME_IMAGE,&ticket))return false;
    if(alloc_budget!=UINT32_MAX)alloc_budget--;
    *out=ticket.frame;*generation=ticket.generation;
    return true;
}
static bool image_phys_guard(void *ctx,const void *ptr,uint64_t generation,
                             uint64_t *physical){
    (void)ctx;
    struct nova_frame_ticket ticket={
        .frame=(void*)ptr,.generation=generation,.role=NOVA_FRAME_IMAGE};
    return nova_pmm_physical_ticket(&ticket,physical);
}
static bool image_release_guard(void *ctx,void *ptr,uint64_t generation){
    (void)ctx;
    struct nova_frame_ticket ticket={
        .frame=ptr,.generation=generation,.role=NOVA_FRAME_IMAGE};
    return nova_page_free_ticket(&ticket);
}
static bool image_phys_legacy(void *ctx,const void *ptr,uint64_t *physical){
    (void)ctx;
    return nova_pmm_physical_role(ptr,NOVA_FRAME_IMAGE,physical);
}
static void image_free_legacy(void *ctx,void *ptr){
    (void)ctx;
    assert(nova_page_free_role(ptr,NOVA_FRAME_IMAGE));
}
static const struct nova_process_guard_ops guard={
    .allocate=image_alloc,.physical=image_phys_guard,.release=image_release_guard
};
static enum nova_process_result stage(struct nova_process_table *table,
                                      const struct nova_process_guard_ops *g,
                                      uint32_t *pid){
    return nova_process_stage_guarded(table,"RAM:/TEST.ELF",read_elf,NULL,
        ELF_BYTES,image_phys_legacy,g,NULL,pid);
}
static void intact(const uint8_t *frame,uint8_t byte){
    for(unsigned i=0;i<4096;i++)assert(frame[i]==byte);
}
static void test_guarded_staging_and_drop(void){
    setup();
    struct nova_process_table table={0};
    uint32_t pid=0;
    assert(stage(&table,&guard,&pid)==NOVA_PROCESS_OK);
    struct nova_process_image *image=nova_process_find(&table,pid);
    assert(image&&image->guarded&&image->page_count==2&&nova_pmm.in_use==2);
    assert(image->pages[0].generation!=0);
    assert(image->pages[1].generation>image->pages[0].generation);
    assert(image->pages[0].frame[0]==1);
    assert(image->pages[1].frame[0]==0x80);
    assert(!nova_process_frames_releasable(image,&table,image_phys_legacy,NULL));
    /* Pointer-only drop must not accidentally erase guarded pages. */
    assert(!nova_process_drop(&table,pid,image_free_legacy,image_phys_legacy,NULL));
    assert(nova_pmm.in_use==2);
    assert(nova_process_find(&table,pid)==NULL); /* descriptor dropped */
    /* Test the normal guarded cleanup with a fresh staged image. */
    setup();table=(struct nova_process_table){0};
    assert(stage(&table,&guard,&pid)==NOVA_PROCESS_OK);
    assert(nova_process_drop_guarded(&table,pid,image_phys_legacy,&guard,NULL));
    assert(nova_pmm.in_use==0 && nova_pmm_metadata_valid());
    assert(nova_process_find(&table,pid)==NULL);
    intact(ram[0],0);
    intact(ram[1],0);
}
static void test_stale_same_role_reuse_fails_closed(void){
    setup();
    struct nova_process_table table={0};
    uint32_t pid=0;
    assert(stage(&table,&guard,&pid)==NOVA_PROCESS_OK);
    struct nova_process_image *image=nova_process_find(&table,pid);
    assert(image);
    struct nova_frame_ticket old={
        .frame=image->pages[0].frame,
        .generation=image->pages[0].generation,.role=NOVA_FRAME_IMAGE};
    assert(nova_page_free_ticket(&old));
    struct nova_frame_ticket replacement={0};
    assert(nova_page_alloc_ticket(NOVA_FRAME_IMAGE,&replacement));
    assert(replacement.frame==old.frame && replacement.generation!=old.generation);
    memset(replacement.frame,0xC3,4096);
    assert(!nova_process_drop_guarded(&table,pid,image_phys_legacy,&guard,NULL));
    intact(replacement.frame,0xC3);
    assert(nova_pmm_ticket_valid(&replacement));
    assert(nova_pmm.in_use==2); /* no other page released on failed preflight */
    assert(nova_process_find(&table,pid)==NULL);
    assert(nova_page_free_ticket(&replacement));
}
static void test_corrupted_generation_and_physical_identity(void){
    for(unsigned fault=0;fault<3;fault++){
        setup();
        struct nova_process_table table={0};
        uint32_t pid=0;
        assert(stage(&table,&guard,&pid)==NOVA_PROCESS_OK);
        struct nova_process_image *image=nova_process_find(&table,pid);
        assert(image);
        uint8_t *first=image->pages[0].frame;
        uint8_t *second=image->pages[1].frame;
        memset(first,0x77,4096);
        memset(second,0x99,4096);
        if(fault==0)image->pages[0].generation++;
        if(fault==1)image->pages[1].generation=0;
        if(fault==2)image->pages[0].physical_page+=4096;
        assert(!nova_process_drop_guarded(&table,pid,image_phys_legacy,&guard,NULL));
        intact(first,0x77);
        intact(second,0x99);
        assert(nova_pmm.in_use==2);
        assert(nova_pmm_metadata_valid());
    }
}
static void test_guarded_rollbacks(void){
    setup();
    struct nova_process_table table={0};
    uint32_t pid=123;
    deny_second_read=true;
    assert(stage(&table,&guard,&pid)==NOVA_PROCESS_READ_ERROR);
    assert(pid==0&&nova_pmm.in_use==0);
    assert(nova_process_count(&table)==0);
    setup();table=(struct nova_process_table){0};pid=123;
    alloc_budget=1;
    assert(stage(&table,&guard,&pid)==NOVA_PROCESS_NO_MEMORY);
    assert(pid==0&&nova_pmm.in_use==0);
    assert(nova_process_count(&table)==0);
}
static bool forged_allocate(void *ctx,void **out,uint64_t *generation){
    if(!image_alloc(ctx,out,generation))return false;
    memset(*out,0xB2,4096);
    (*generation)++; /* forged ticket must not authorize touching the frame */
    return true;
}
static void test_forged_ticket_never_wipes_frame(void){
    setup();
    const struct nova_process_guard_ops forged={
        .allocate=forged_allocate,
        .physical=image_phys_guard,
        .release=image_release_guard
    };
    struct nova_process_table table={0};
    uint32_t pid=123;
    assert(stage(&table,&forged,&pid)==NOVA_PROCESS_BAD_FRAME);
    assert(pid==0&&nova_process_count(&table)==0);
    assert(nova_pmm.in_use==1); /* suspect frame is not released */
    intact(ram[0],0xB2);
}
int main(void){
    assert(strcmp(nova_elf_result_name(NOVA_ELF_OK),
                  "VALID STATIC ELF64 (NOT EXECUTED)")==0);
    test_guarded_staging_and_drop();
    test_stale_same_role_reuse_fails_closed();
    test_corrupted_generation_and_physical_identity();
    test_guarded_rollbacks();
    test_forged_ticket_never_wipes_frame();
    puts("Staged ELF generation tickets: ABA, corrupt identity, rollback and pointer-only drop checks OK.");
    return 0;
}
