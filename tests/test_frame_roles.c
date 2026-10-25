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
int main(void){
    test_role_transitions_and_corruption();
    test_offline_role_integration();
    puts("PMM frame roles: cross-class frees rejected, metadata corruption blocked, staged ELF/table/stack integration OK.");
    return 0;
}
