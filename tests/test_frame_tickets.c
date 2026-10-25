/* Host-only regression tests for PMM generation tickets. No VM, disk,
 * ISO, CR3, or kernel execution. Uses the synthetic Limine fixture. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/pmm.h"

#define TEST_FRAMES 16u
static _Alignas(4096) uint8_t ram[TEST_FRAMES][4096];

static void setup(void){
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
    assert(nova_pmm.next_generation==1);
}
static void unchanged(const uint8_t *frame,uint8_t value){
    for(unsigned i=0;i<4096;i++)assert(frame[i]==value);
}
static void test_same_role_reuse_rejects_stale_ticket(void){
    setup();
    struct nova_frame_ticket a={0},b={0};
    assert(!nova_page_alloc_ticket(NOVA_FRAME_IMAGE,NULL));
    assert(!nova_page_alloc_ticket(NOVA_FRAME_FREE,&a));
    assert(a.frame==NULL && a.generation==0);
    assert(nova_page_alloc_ticket(NOVA_FRAME_IMAGE,&a));
    assert(a.frame==ram[0] && a.generation==1 && a.role==NOVA_FRAME_IMAGE);
    assert(nova_pmm_ticket_valid(&a));
    uint64_t phys=0;
    assert(nova_pmm_physical_ticket(&a,&phys)&&phys==0x200000u);
    assert(nova_page_free_ticket(&a));
    assert(!nova_pmm_ticket_valid(&a));
    assert(!nova_page_free_ticket(&a));
    assert(nova_page_alloc_ticket(NOVA_FRAME_IMAGE,&b));
    assert(b.frame==a.frame && b.generation>a.generation &&
           b.role==a.role);
    memset(b.frame,0xD3,4096);
    phys=0xC0FFEE;
    assert(!nova_pmm_physical_ticket(&a,&phys)&&phys==0xC0FFEE);
    assert(!nova_page_free_ticket(&a));
    unchanged(b.frame,0xD3);
    assert(nova_pmm_ticket_valid(&b));
    struct nova_frame_ticket forged=b;
    forged.generation=a.generation;
    assert(!nova_pmm_ticket_valid(&forged));
    forged=b;forged.role=NOVA_FRAME_STACK;
    assert(!nova_page_free_ticket(&forged));
    forged=b;forged.frame=(uint8_t*)b.frame+1;
    assert(!nova_page_free_ticket(&forged));
    forged=b;forged.frame=ram[TEST_FRAMES-1u];
    assert(!nova_page_free_ticket(&forged));
    forged=b;forged.generation=0;
    assert(!nova_page_free_ticket(&forged));
    unchanged(b.frame,0xD3);
    assert(nova_page_free_ticket(&b));
    unchanged(ram[0],0);
    assert(nova_pmm.in_use==0 && nova_pmm_metadata_valid());
}
static void test_cross_role_and_legacy_allocation(void){
    setup();
    struct nova_frame_ticket image={0},stack={0};
    assert(nova_page_alloc_ticket(NOVA_FRAME_IMAGE,&image));
    assert(nova_page_free_ticket(&image));
    assert(nova_page_alloc_ticket(NOVA_FRAME_STACK,&stack));
    assert(stack.frame==image.frame && stack.generation!=image.generation);
    memset(stack.frame,0x67,4096);
    assert(!nova_page_free_ticket(&image));
    unchanged(stack.frame,0x67);
    assert(nova_page_free_ticket(&stack));
    /* Pointer-only allocations also advance generations, so old tickets
     * cannot free them even if the recycled role is identical. */
    assert(nova_page_alloc_ticket(NOVA_FRAME_IMAGE,&image));
    assert(nova_page_free_ticket(&image));
    void *legacy=nova_page_alloc_role(NOVA_FRAME_IMAGE);
    assert(legacy==image.frame);
    memset(legacy,0x51,4096);
    assert(!nova_page_free_ticket(&image));
    unchanged(legacy,0x51);
    assert(nova_page_free_role(legacy,NOVA_FRAME_IMAGE));
    assert(nova_pmm_metadata_valid());
    assert(nova_pmm_self_test()); /* legacy PMM self-test remains usable */
}
static void test_corruption_and_generation_exhaustion(void){
    setup();
    struct nova_frame_ticket ticket={0},next={0};
    assert(nova_page_alloc_ticket(NOVA_FRAME_PAGE_TABLE,&ticket));
    memset(ticket.frame,0x91,4096);
    const struct nova_page_manager good=nova_pmm;
    for(unsigned fault=0;fault<5;fault++){
        nova_pmm=good;
        switch(fault){
            case 0:nova_pmm.generations[0]=0;break;
            case 1:nova_pmm.generations[0]=nova_pmm.next_generation;break;
            case 2:nova_pmm.generations[1]=1;break; /* free frame */
            case 3:nova_pmm.next_generation=0;break;
            case 4:nova_pmm.generations[NOVA_PAGE_POOL_CAP-1u]=1;break;
            default:assert(false);
        }
        assert(!nova_pmm_metadata_valid());
        assert(!nova_pmm_ticket_valid(&ticket));
        assert(!nova_page_free_ticket(&ticket));
        assert(!nova_page_alloc_ticket(NOVA_FRAME_IMAGE,&next));
        assert(next.frame==NULL && next.generation==0);
        unchanged(ticket.frame,0x91);
    }
    nova_pmm=good;
    assert(nova_pmm_ticket_valid(&ticket));
    /* A counter at the limit must not wrap and issue an old identity. */
    nova_pmm.next_generation=UINT64_MAX;
    assert(nova_pmm_metadata_valid());
    assert(!nova_page_alloc_ticket(NOVA_FRAME_IMAGE,&next));
    assert(next.frame==NULL);
    assert(nova_pmm.in_use==1 && nova_pmm.next_generation==UINT64_MAX);
    assert(nova_page_free_ticket(&ticket));
    assert(nova_pmm.in_use==0 && nova_pmm_metadata_valid());
    assert(nova_page_alloc_role(NOVA_FRAME_IMAGE)==NULL);
    assert(nova_pmm.next_generation==UINT64_MAX);
}
int main(void){
    test_same_role_reuse_rejects_stale_ticket();
    test_cross_role_and_legacy_allocation();
    test_corruption_and_generation_exhaustion();
    puts("PMM generations: stale same-role tickets rejected; corruption and counter exhaustion fail closed.");
    return 0;
}
