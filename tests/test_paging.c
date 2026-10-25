/* Page-table host tests: CPU page-table registers are NEVER modified.
 * Simulated page-aligned physical frames exercise the real page-table bits.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/paging.h"
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
int main(void){
    test_page_tables();
    test_isolated_roots();
    test_failures();
    assert(active==0);
    puts("Offline paging: 4-level PML4 trees, USER/RW/NX bits, separate roots, rollback and no CR3 use.");
    return 0;
}
