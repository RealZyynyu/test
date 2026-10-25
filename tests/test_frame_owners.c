/* Owner-tagged PMM tickets: host-only synthetic RAM, no disk/VM/ISO. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/pmm.h"

#define TEST_FRAMES 16u
static uint64_t owner_a,owner_b;
#define OWNER_A owner_a
#define OWNER_B owner_b
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
    assert(nova_owner_create(&owner_a) && nova_owner_create(&owner_b));
    assert(owner_a!=owner_b && nova_pmm_metadata_valid());
}
static void unchanged(const uint8_t *p,uint8_t value){
    for(unsigned i=0;i<4096;i++)assert(p[i]==value);
}
static void test_owner_separation_and_legacy_denial(void){
    setup();
    struct nova_frame_ticket a={0},b={0},legacy={0};
    assert(!nova_page_alloc_owned_ticket(NOVA_FRAME_IMAGE,OWNER_A,NULL));
    assert(!nova_page_alloc_owned_ticket(NOVA_FRAME_IMAGE,0,&a));
    assert(a.frame==NULL && a.generation==0 && a.owner==0);
    assert(!nova_page_alloc_owned_ticket(NOVA_FRAME_FREE,OWNER_A,&a));
    assert(a.frame==NULL);
    assert(nova_page_alloc_owned_ticket(NOVA_FRAME_IMAGE,OWNER_A,&a));
    assert(nova_page_alloc_owned_ticket(NOVA_FRAME_IMAGE,OWNER_B,&b));
    assert(a.frame==ram[0] && b.frame==ram[1] && a.owner==OWNER_A &&
           b.owner==OWNER_B && a.generation!=b.generation);
    assert(nova_pmm_ticket_valid(&a) && nova_pmm_ticket_valid(&b));
    uint64_t phys=0;
    assert(nova_pmm_physical_ticket(&a,&phys) && phys==0x200000u);
    assert(nova_pmm_physical_ticket(&b,&phys) && phys==0x201000u);
    phys=UINT64_C(0xBAD);
    enum nova_frame_role role=NOVA_FRAME_FREE;
    assert(!nova_pmm_physical(a.frame,&phys) && phys==UINT64_C(0xBAD));
    assert(!nova_pmm_physical_role(a.frame,NOVA_FRAME_IMAGE,&phys));
    assert(!nova_pmm_frame_role(a.frame,&role));
    assert(!nova_page_free_role(a.frame,NOVA_FRAME_IMAGE));
    assert(!nova_page_free(a.frame));
    memset(a.frame,0x5C,4096);
    memset(b.frame,0x7D,4096);
    struct nova_frame_ticket forged=a;
    forged.owner=OWNER_B;
    assert(!nova_pmm_ticket_valid(&forged));
    assert(!nova_page_free_ticket(&forged));
    assert(!nova_pmm_physical_ticket(&forged,&phys) && phys==UINT64_C(0xBAD));
    forged=a;forged.owner=0;
    assert(!nova_page_free_ticket(&forged));
    forged=a;forged.generation=b.generation;
    assert(!nova_page_free_ticket(&forged));
    unchanged(a.frame,0x5C);
    unchanged(b.frame,0x7D);
    assert(nova_page_alloc_ticket(NOVA_FRAME_IMAGE,&legacy));
    assert(legacy.owner==0 && nova_pmm_ticket_valid(&legacy));
    assert(nova_page_free_ticket(&legacy));
    assert(nova_page_free_ticket(&a));
    unchanged(ram[0],0);
    assert(nova_page_free_ticket(&b));
    assert(nova_pmm.in_use==0 && nova_pmm_metadata_valid());
    assert(nova_pmm_self_test()); /* legacy self-test remains available */
}
static void test_owner_reuse_and_generation(void){
    setup();
    struct nova_frame_ticket old={0},newer={0};
    assert(nova_page_alloc_owned_ticket(NOVA_FRAME_STACK,OWNER_A,&old));
    assert(nova_page_free_ticket(&old));
    assert(nova_page_alloc_owned_ticket(NOVA_FRAME_STACK,OWNER_B,&newer));
    assert(newer.frame==old.frame && newer.generation!=old.generation);
    memset(newer.frame,0xE2,4096);
    assert(!nova_page_free_ticket(&old));
    unchanged(newer.frame,0xE2);
    assert(nova_page_free_ticket(&newer));
    assert(nova_page_alloc_owned_ticket(NOVA_FRAME_STACK,OWNER_A,&newer));
    assert(newer.frame==old.frame && newer.generation!=old.generation);
    memset(newer.frame,0xC3,4096);
    assert(!nova_page_free_ticket(&old));
    unchanged(newer.frame,0xC3);
    assert(nova_page_free_ticket(&newer));
    assert(nova_pmm_metadata_valid());
}
static void test_owner_ledger_corruption_and_exhaustion(void){
    setup();
    struct nova_frame_ticket a={0},out={0};
    assert(nova_page_alloc_owned_ticket(NOVA_FRAME_PAGE_TABLE,OWNER_A,&a));
    memset(a.frame,0x91,4096);
    const struct nova_page_manager good=nova_pmm;
    for(unsigned fault=0;fault<2;fault++){
        nova_pmm=good;
        if(fault==0)nova_pmm.owners[1]=OWNER_B; /* free pool slot */
        else nova_pmm.owners[NOVA_PAGE_POOL_CAP-1u]=OWNER_B;
        assert(!nova_pmm_metadata_valid());
        assert(!nova_page_free_ticket(&a));
        assert(!nova_page_alloc_owned_ticket(NOVA_FRAME_STACK,OWNER_B,&out));
        assert(out.frame==NULL && out.owner==0);
        unchanged(a.frame,0x91);
    }
    nova_pmm=good;
    /* Plausible but wrong owner ledger: original ticket must fail closed.
     * A malicious kernel with forged ledger + ticket is out of scope. */
    nova_pmm.owners[0]=OWNER_B;
    assert(nova_pmm_metadata_valid());
    assert(!nova_pmm_ticket_valid(&a));
    assert(!nova_page_free_ticket(&a));
    unchanged(a.frame,0x91);
    nova_pmm=good;
    nova_pmm.next_generation=UINT64_MAX;
    assert(nova_pmm_metadata_valid());
    assert(!nova_page_alloc_owned_ticket(NOVA_FRAME_STACK,OWNER_B,&out));
    assert(out.frame==NULL && out.owner==0);
    assert(nova_page_free_ticket(&a));
    assert(nova_pmm.in_use==0 && nova_pmm_metadata_valid());
}
int main(void){
    test_owner_separation_and_legacy_denial();
    test_owner_reuse_and_generation();
    test_owner_ledger_corruption_and_exhaustion();
    puts("PMM owner tickets: cross-owner and legacy access denied; ABA and corrupt ledgers fail closed.");
    return 0;
}
