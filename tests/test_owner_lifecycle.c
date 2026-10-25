/* PMM trusted owner lifecycle: synthetic host memory only, no disk/ISO. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/pmm.h"
#define FRAMES 16u
static _Alignas(4096) uint8_t ram[FRAMES][4096];
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
}
static void unchanged(const uint8_t *p,uint8_t v){
    for(unsigned i=0;i<4096;i++)assert(p[i]==v);
}
static void lifecycle(void){
    setup();
    uint64_t a=0,b=0,c=0;
    assert(!nova_owner_create(NULL));
    assert(!nova_owner_retire(0) && !nova_owner_retire(42));
    assert(nova_owner_create(&a) && nova_owner_create(&b) && a && b>a);
    struct nova_frame_ticket ticket={0},replacement={0};
    assert(!nova_page_alloc_owned_ticket(NOVA_FRAME_STACK,b+1,&replacement));
    assert(replacement.frame==NULL);
    assert(nova_page_alloc_owned_ticket(NOVA_FRAME_STACK,a,&ticket));
    memset(ticket.frame,0x4D,4096);
    assert(!nova_owner_retire(a) && nova_pmm_ticket_valid(&ticket));
    unchanged(ticket.frame,0x4D);
    assert(nova_page_free_ticket(&ticket));
    assert(nova_owner_retire(a) && !nova_owner_retire(a));
    assert(!nova_page_alloc_owned_ticket(NOVA_FRAME_STACK,a,&replacement));
    assert(nova_owner_create(&c) && c>b && c!=a);
    assert(nova_page_alloc_owned_ticket(NOVA_FRAME_STACK,c,&replacement));
    assert(replacement.frame==ticket.frame &&
           replacement.generation!=ticket.generation);
    memset(replacement.frame,0xC9,4096);
    assert(!nova_page_free_ticket(&ticket));
    unchanged(replacement.frame,0xC9);
    assert(nova_page_free_ticket(&replacement));
    assert(nova_owner_retire(b) && nova_owner_retire(c));
    assert(nova_pmm_metadata_valid() && nova_pmm.in_use==0);
}
static void capacity(void){
    setup();
    uint64_t ids[NOVA_OWNER_SLOT_CAP]={0};
    for(uint32_t i=0;i<NOVA_OWNER_SLOT_CAP;i++){
        assert(nova_owner_create(&ids[i]));
        assert(ids[i]==(uint64_t)i+1u);
    }
    uint64_t next=42;
    assert(!nova_owner_create(&next) && next==0);
    assert(nova_owner_retire(ids[3]));
    assert(nova_owner_create(&next) && next>ids[NOVA_OWNER_SLOT_CAP-1u]);
    assert(next!=ids[3] && nova_pmm_metadata_valid());
    struct nova_frame_ticket ticket={0};
    assert(!nova_page_alloc_owned_ticket(NOVA_FRAME_IMAGE,ids[3],&ticket));
    assert(ticket.frame==NULL);
    nova_pmm.next_owner=UINT64_MAX;
    assert(nova_pmm_metadata_valid());
    next=42;
    assert(!nova_owner_create(&next) && next==0);
    assert(nova_owner_retire(ids[0]));
}
static void corruption(void){
    setup();
    uint64_t a=0,b=0;
    assert(nova_owner_create(&a) && nova_owner_create(&b));
    struct nova_frame_ticket ticket={0},next={0};
    assert(nova_page_alloc_owned_ticket(NOVA_FRAME_IMAGE,a,&ticket));
    memset(ticket.frame,0x71,4096);
    const struct nova_page_manager good=nova_pmm;
    for(unsigned fault=0;fault<5;fault++){
        nova_pmm=good;
        switch(fault){
            case 0:nova_pmm.owner_registry[0]=0;break;
            case 1:nova_pmm.owner_registry[1]=a;break;
            case 2:nova_pmm.owner_registry[1]=nova_pmm.next_owner;break;
            case 3:nova_pmm.next_owner=0;break;
            case 4:nova_pmm.owner_registry[NOVA_OWNER_SLOT_CAP-1u]=a;break;
            default:assert(0);
        }
        assert(!nova_pmm_metadata_valid());
        assert(!nova_pmm_ticket_valid(&ticket));
        assert(!nova_page_free_ticket(&ticket));
        assert(!nova_page_alloc_owned_ticket(NOVA_FRAME_IMAGE,b,&next));
        assert(next.frame==NULL);
        uint64_t out=42;
        assert(!nova_owner_create(&out) && out==0);
        assert(!nova_owner_retire(a));
        unchanged(ticket.frame,0x71);
    }
    nova_pmm=good;
    assert(nova_pmm_metadata_valid() && nova_page_free_ticket(&ticket));
    assert(nova_owner_retire(a) && nova_owner_retire(b));
    assert(nova_pmm_self_test()); /* legacy path remains usable */
}
int main(void){
    lifecycle();capacity();corruption();
    puts("Owner lifecycle: bounded IDs, live-frame retirement denial, ABA and corruption fail closed.");
    return 0;
}
