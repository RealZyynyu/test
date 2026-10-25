/* Host-side staged-process tests. No CPU privilege changes or code execution. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/process_image.h"

#define IMAGE_SIZE 16384u
#define TEST_POOL 16u
static uint8_t image[IMAGE_SIZE];
static _Alignas(4096) uint8_t frames[TEST_POOL][4096];
static bool used[TEST_POOL];
static uint32_t active_pages=0;
static uint32_t allocations_before_failure=UINT32_MAX;
static bool reject_segment_read=false;
static void w16(size_t p,uint16_t v){image[p]=(uint8_t)v;image[p+1]=(uint8_t)(v>>8);}
static void w32(size_t p,uint32_t v){
    for(unsigned i=0;i<4;i++)image[p+i]=(uint8_t)(v>>(8*i));
}
static void w64(size_t p,uint64_t v){
    for(unsigned i=0;i<8;i++)image[p+i]=(uint8_t)(v>>(8*i));
}
static void segment(unsigned i,uint32_t flags,uint64_t file_off,
                    uint64_t va,uint64_t filesz,uint64_t memsz){
    size_t p=64u+i*56u;
    w32(p,1);w32(p+4,flags);w64(p+8,file_off);w64(p+16,va);
    w64(p+32,filesz);w64(p+40,memsz);w64(p+48,4096);
}
static void fixture(void){
    memset(image,0,sizeof(image));
    image[0]=0x7f;image[1]='E';image[2]='L';image[3]='F';
    image[4]=2;image[5]=1;image[6]=1;
    w16(16,2);w16(18,62);w32(20,1);w64(24,0x401000);w64(32,64);
    w16(52,64);w16(54,56);w16(56,2);
    segment(0,5,0x1000,0x401000,128,4096);
    segment(1,6,0x2000,0x402000,200,8192);
    for(unsigned i=0;i<128;i++)image[0x1000+i]=(uint8_t)(i+1);
    for(unsigned i=0;i<200;i++)image[0x2000+i]=(uint8_t)(0xAA^(uint8_t)i);
}
static bool read_image(void *ctx,uint64_t off,uint8_t *out,size_t n){
    (void)ctx;
    if(reject_segment_read && off>=0x1000)return false;
    if(off>IMAGE_SIZE || n>IMAGE_SIZE-off)return false;
    memcpy(out,image+off,n);return true;
}
static void *alloc_page(void *ctx){
    (void)ctx;
    if(allocations_before_failure==0)return NULL;
    if(allocations_before_failure!=UINT32_MAX)allocations_before_failure--;
    for(unsigned i=0;i<TEST_POOL;i++){
        if(used[i])continue;
        used[i]=true;active_pages++;
        memset(frames[i],0xD9,4096); /* allocator intentionally leaks old bytes */
        return frames[i];
    }
    return NULL;
}
static void free_page(void *ctx,void *ptr){
    (void)ctx;
    for(unsigned i=0;i<TEST_POOL;i++)if(ptr==frames[i]){
        assert(used[i]);used[i]=false;active_pages--;return;
    }
    assert(!"invalid frame pointer");
}
/* Simulated physical identities: separate from host pointers and valid
 * only while the synthetic frame allocator marks a frame allocated. */
static bool physical_page(void *ctx,const void *frame,uint64_t *out){
    (void)ctx;
    if(!out)return false;
    for(unsigned i=0;i<TEST_POOL;i++)if(frame==frames[i] && used[i]){
        *out=UINT64_C(0x200000)+(uint64_t)i*4096u;
        return true;
    }
    return false;
}
struct physical_fault {
    const void *target;
    uint64_t reported;
    bool deny;
};
static bool faulty_physical(void *ctx,const void *frame,uint64_t *out){
    struct physical_fault *fault=(struct physical_fault *)ctx;
    if(!physical_page(NULL,frame,out))return false;
    if(frame==fault->target){
        if(fault->deny)return false;
        *out=fault->reported;
    }
    return true;
}
static enum nova_process_result stage(struct nova_process_table *table,
                                      uint32_t *pid){
    return nova_process_stage(table,"DISK:/APP.ELF",read_image,NULL,IMAGE_SIZE,
                              alloc_page,free_page,physical_page,NULL,pid);
}
static void test_staging(void){
    fixture();
    struct nova_process_table tab={0};
    uint32_t pid=0;
    assert(nova_process_count(&tab)==0);
    assert(stage(&tab,&pid)==NOVA_PROCESS_OK);
    assert(pid==1&&nova_process_count(&tab)==1&&active_pages==3);
    struct nova_process_image *proc=nova_process_find(&tab,pid);
    assert(proc&&proc->entry==0x401000&&proc->page_count==3&&proc->segments==2);
    assert(proc->pages[0].flags==5&&proc->pages[1].flags==6);
    assert(proc->pages[0].virtual_page==0x401000);
    assert(proc->pages[1].virtual_page==0x402000);
    assert(proc->pages[2].virtual_page==0x403000);
    assert(!strcmp(proc->label,"DISK:/APP.ELF"));
    uint8_t out[16];
    assert(nova_process_peek(proc,0x401000,out,16));
    for(unsigned i=0;i<16;i++)assert(out[i]==i+1);
    assert(nova_process_peek(proc,0x402010,out,16));
    for(unsigned i=0;i<16;i++)assert(out[i]==(uint8_t)(0xAA^(uint8_t)(16+i)));
    assert(nova_process_peek(proc,0x402300,out,16));
    for(unsigned i=0;i<16;i++)assert(out[i]==0); /* BSS clean */
    assert(nova_process_peek(proc,0x403000,out,16));
    for(unsigned i=0;i<16;i++)assert(out[i]==0); /* next BSS page */
    assert(!nova_process_peek(proc,0x400000,out,1));
    assert(!nova_process_peek(proc,UINT64_MAX,out,2));
    assert(!nova_process_peek(proc,0x401000,NULL,1));
    assert(nova_process_peek(proc,0x401000,NULL,0));
    assert(nova_process_drop(&tab,pid,free_page,physical_page,NULL));
    assert(active_pages==0 && nova_process_count(&tab)==0);
    assert(!nova_process_drop(&tab,pid,free_page,physical_page,NULL));
    assert(!nova_process_find(&tab,pid));
    assert(!strcmp(nova_process_result_name(NOVA_PROCESS_OK),
                   "IMAGE STAGED - NOT EXECUTABLE YET"));
    assert(!strcmp(nova_elf_result_name(NOVA_ELF_OK),
                   "VALID STATIC ELF64 (NOT EXECUTED)"));
}
static void test_failures(void){
    fixture();struct nova_process_table tab={0};uint32_t pid=88;
    assert(nova_process_stage(NULL,"APP",read_image,NULL,IMAGE_SIZE,
              alloc_page,free_page,physical_page,NULL,&pid)==NOVA_PROCESS_BAD_INPUT);
    assert(pid==0);
    assert(nova_process_stage(&tab,"APP",read_image,NULL,IMAGE_SIZE,
              alloc_page,free_page,physical_page,NULL,NULL)==NOVA_PROCESS_BAD_INPUT);
    w32(64+4,7); /* unsafe executable+writable */
    assert(stage(&tab,&pid)==NOVA_PROCESS_INVALID_ELF);
    assert(active_pages==0 && nova_process_count(&tab)==0);
    fixture();
    allocations_before_failure=1;
    assert(stage(&tab,&pid)==NOVA_PROCESS_NO_MEMORY);
    assert(active_pages==0&&nova_process_count(&tab)==0&&pid==0);
    allocations_before_failure=UINT32_MAX;
    reject_segment_read=true;
    assert(stage(&tab,&pid)==NOVA_PROCESS_READ_ERROR);
    assert(active_pages==0 && nova_process_count(&tab)==0);
    reject_segment_read=false;
    /* 33 pages exceeds per-image limit; ELF itself accepts up to 4096. */
    segment(1,6,0x2000,0x402000,200,4096u*33u);
    assert(stage(&tab,&pid)==NOVA_PROCESS_TOO_LARGE);
    assert(active_pages==0);
    fixture();
    uint32_t ids[NOVA_PROCESS_SLOTS];
    for(unsigned i=0;i<NOVA_PROCESS_SLOTS;i++){
        assert(stage(&tab,&ids[i])==NOVA_PROCESS_OK);
        assert(ids[i]==i+1);
    }
    assert(nova_process_count(&tab)==NOVA_PROCESS_SLOTS);
    assert(active_pages==NOVA_PROCESS_SLOTS*3u);
    assert(stage(&tab,&pid)==NOVA_PROCESS_NO_SLOT);
    for(unsigned i=0;i<NOVA_PROCESS_SLOTS;i++)
        assert(nova_process_drop(&tab,ids[i],free_page,physical_page,NULL));
    assert(active_pages==0&&nova_process_count(&tab)==0);
    assert(stage(&tab,&pid)==NOVA_PROCESS_OK&&pid==5); /* monotonic IDs */
    assert(nova_process_drop(&tab,pid,free_page,physical_page,NULL));
}
/* A malicious allocator can return a borrowed page. It must be rejected
 * before zeroing any bytes and must never be released as a new allocation. */
struct borrowed_ctx {
    unsigned calls;
    unsigned borrow_on;
    uint8_t *borrowed;
    bool check_original_on_free;
};
static void *borrow_alloc(void *ctx){
    struct borrowed_ctx *b=(struct borrowed_ctx *)ctx;
    if(++b->calls==b->borrow_on)return b->borrowed;
    return alloc_page(NULL);
}
static void borrow_free(void *ctx,void *frame){
    struct borrowed_ctx *b=(struct borrowed_ctx *)ctx;
    if(b->check_original_on_free && frame==frames[0]){
        /* First segment must have survived the rejected second allocation. */
        assert(frames[0][0]==1 && frames[0][127]==128);
    }
    free_page(NULL,frame);
}
static enum nova_process_result stage_borrowed(
    struct nova_process_table *table,struct borrowed_ctx *ctx,uint32_t *pid){
    return nova_process_stage(table,"DISK:/APP.ELF",read_image,NULL,IMAGE_SIZE,
                              borrow_alloc,borrow_free,physical_page,ctx,pid);
}
static void test_borrowed_frame_rejected_before_write(void){
    fixture();
    struct nova_process_table table={0};
    uint32_t pid=99;
    struct borrowed_ctx same={.borrow_on=2,.borrowed=frames[0],
                              .check_original_on_free=true};
    assert(stage_borrowed(&table,&same,&pid)==NOVA_PROCESS_BAD_FRAME);
    assert(pid==0 && active_pages==0 && nova_process_count(&table)==0);
    /* The alias is an interior pointer into the previously allocated page. */
    struct borrowed_ctx interior={.borrow_on=2,.borrowed=frames[0]+128,
                                  .check_original_on_free=true};
    assert(stage_borrowed(&table,&interior,&pid)==NOVA_PROCESS_BAD_FRAME);
    assert(pid==0 && active_pages==0 && nova_process_count(&table)==0);
    /* A non-aligned fresh pointer must not be zeroed or released. */
    memset(frames[0],0xA5,4096);
    struct borrowed_ctx misaligned={.borrow_on=1,.borrowed=frames[0]+1};
    assert(stage_borrowed(&table,&misaligned,&pid)==NOVA_PROCESS_BAD_FRAME);
    for(unsigned i=0;i<4096;i++)assert(frames[0][i]==0xA5);
    assert(active_pages==0);
    /* A frame already owned by another staged process must stay intact. */
    fixture();
    uint32_t original=0;
    assert(stage(&table,&original)==NOVA_PROCESS_OK);
    assert(active_pages==3);
    uint8_t snapshot[4096];
    memcpy(snapshot,frames[0],sizeof(snapshot));
    struct borrowed_ctx foreign={.borrow_on=1,.borrowed=frames[0]};
    assert(stage_borrowed(&table,&foreign,&pid)==NOVA_PROCESS_BAD_FRAME);
    assert(pid==0 && active_pages==3 && nova_process_count(&table)==1);
    assert(memcmp(snapshot,frames[0],sizeof(snapshot))==0);
    assert(nova_process_drop(&table,original,free_page,physical_page,NULL));
    assert(active_pages==0);
}
static void test_corrupted_image_teardown_fails_closed(void){
    fixture();
    struct nova_process_table table={0};
    uint32_t pid=0;
    assert(stage(&table,&pid)==NOVA_PROCESS_OK);
    struct nova_process_image *proc=nova_process_find(&table,pid);
    assert(proc && active_pages==3);
    uint8_t *original[3]={proc->pages[0].frame,proc->pages[1].frame,
                          proc->pages[2].frame};
    proc->page_count=NOVA_PROCESS_MAX_PAGES+1u;
    uint8_t scratch=0;
    assert(!nova_process_peek(proc,0x401000,&scratch,1));
    assert(!nova_process_drop(&table,pid,free_page,physical_page,NULL));
    assert(active_pages==3 && nova_process_count(&table)==0);
    for(unsigned i=0;i<3;i++)free_page(NULL,original[i]);
    assert(active_pages==0);

    assert(stage(&table,&pid)==NOVA_PROCESS_OK);
    proc=nova_process_find(&table,pid);
    assert(proc);
    for(unsigned i=0;i<3;i++)original[i]=proc->pages[i].frame;
    proc->pages[1].frame=proc->pages[0].frame;
    assert(!nova_process_drop(&table,pid,free_page,physical_page,NULL));
    assert(active_pages==3);
    for(unsigned i=0;i<3;i++)free_page(NULL,original[i]);
    assert(active_pages==0);

    /* A forged cross-process alias must not free the other image's frame. */
    uint32_t first=0,second=0;
    assert(stage(&table,&first)==NOVA_PROCESS_OK);
    assert(stage(&table,&second)==NOVA_PROCESS_OK);
    assert(active_pages==6);
    struct nova_process_image *other=nova_process_find(&table,second);
    struct nova_process_image *owner=nova_process_find(&table,first);
    assert(other && owner);
    for(unsigned i=0;i<3;i++)original[i]=other->pages[i].frame;
    other->pages[0].frame=owner->pages[0].frame;
    assert(!nova_process_drop(&table,second,free_page,physical_page,NULL));
    assert(active_pages==6 && nova_process_find(&table,first)!=NULL);
    for(unsigned i=0;i<3;i++)free_page(NULL,original[i]);
    assert(nova_process_drop(&table,first,free_page,physical_page,NULL));
    assert(active_pages==0);
}

/* Physical alias rejection: distinct virtual pointers can still designate
 * the SAME machine frame. Reject before touching the suspect page, and do
 * not release a frame whose ownership cannot be established. */
static void test_physical_alias_rejected_before_copy(void){
    fixture();
    struct nova_process_table table={0};
    uint32_t pid=55;
    struct physical_fault same={.target=frames[1],.reported=UINT64_C(0x200000)};
    assert(nova_process_stage(&table,"ALIAS",read_image,NULL,IMAGE_SIZE,
           alloc_page,free_page,faulty_physical,&same,&pid)==NOVA_PROCESS_BAD_FRAME);
    assert(pid==0 && nova_process_count(&table)==0);
    assert(active_pages==1 && used[1] && !used[0]);
    for(unsigned i=0;i<4096;i++)assert(frames[1][i]==0xD9);
    free_page(NULL,frames[1]);
    assert(active_pages==0);

    /* A fresh host pointer reported as another staged image's physical
     * frame must not modify that image's bytes or free its memory. */
    fixture();
    uint32_t owner=0;
    assert(stage(&table,&owner)==NOVA_PROCESS_OK);
    uint8_t snapshot[4096];
    memcpy(snapshot,frames[0],sizeof(snapshot));
    struct physical_fault foreign={.target=frames[3],
                                    .reported=UINT64_C(0x200000)};
    pid=88;
    assert(nova_process_stage(&table,"FOREIGN",read_image,NULL,IMAGE_SIZE,
           alloc_page,free_page,faulty_physical,&foreign,&pid)==NOVA_PROCESS_BAD_FRAME);
    assert(pid==0 && active_pages==4 && used[3]);
    assert(memcmp(snapshot,frames[0],sizeof(snapshot))==0);
    for(unsigned i=0;i<4096;i++)assert(frames[3][i]==0xD9);
    free_page(NULL,frames[3]); /* manual recovery of deliberately leaked page */
    assert(nova_process_drop(&table,owner,free_page,physical_page,NULL));
    assert(active_pages==0);
}
static void test_unverifiable_physical_frame_fails_closed(void){
    const uint64_t invalid[]={0,0x200001u,UINT64_C(0x0010000000000000)};
    for(unsigned k=0;k<sizeof(invalid)/sizeof(invalid[0]);k++){
        fixture();
        struct nova_process_table table={0};
        uint32_t pid=88;
        struct physical_fault bad={.target=frames[0],.reported=invalid[k]};
        assert(nova_process_stage(&table,"INVALID",read_image,NULL,IMAGE_SIZE,
               alloc_page,free_page,faulty_physical,&bad,&pid)==NOVA_PROCESS_BAD_FRAME);
        assert(pid==0 && active_pages==1 && used[0]);
        for(unsigned i=0;i<4096;i++)assert(frames[0][i]==0xD9);
        free_page(NULL,frames[0]);
    }
    fixture();
    struct nova_process_table table={0};
    uint32_t pid=88;
    struct physical_fault denied={.target=frames[0],.deny=true};
    assert(nova_process_stage(&table,"DENIED",read_image,NULL,IMAGE_SIZE,
           alloc_page,free_page,faulty_physical,&denied,&pid)==NOVA_PROCESS_BAD_FRAME);
    assert(active_pages==1 && pid==0);
    free_page(NULL,frames[0]);
    assert(nova_process_stage(&table,"NO_PHYS",read_image,NULL,IMAGE_SIZE,
           alloc_page,free_page,NULL,NULL,&pid)==NOVA_PROCESS_BAD_INPUT);
    assert(active_pages==0);
}
static void test_physical_identity_rechecked_on_drop_and_stage(void){
    fixture();
    struct nova_process_table table={0};
    uint32_t pid=0;
    assert(stage(&table,&pid)==NOVA_PROCESS_OK);
    struct nova_process_image *p=nova_process_find(&table,pid);
    assert(p && active_pages==3);
    uint8_t *saved[3]={p->pages[0].frame,p->pages[1].frame,p->pages[2].frame};
    const uint64_t original=p->pages[0].physical_page;
    assert(!nova_process_drop(&table,pid,free_page,NULL,NULL));
    assert(nova_process_find(&table,pid)==p && active_pages==3);
    p->pages[0].physical_page=p->pages[1].physical_page;
    assert(!nova_process_drop(&table,pid,free_page,physical_page,NULL));
    assert(active_pages==3 && nova_process_find(&table,pid)==NULL);
    for(unsigned i=0;i<3;i++)free_page(NULL,saved[i]);
    assert(active_pages==0);

    assert(stage(&table,&pid)==NOVA_PROCESS_OK);
    p=nova_process_find(&table,pid);
    assert(p);
    p->pages[0].physical_page=UINT64_C(0x200000); /* correct initially */
    struct physical_fault changed={.target=p->pages[0].frame,
                                   .reported=UINT64_C(0x210000)};
    assert(!nova_process_drop(&table,pid,free_page,faulty_physical,&changed));
    assert(active_pages==3);
    for(unsigned i=0;i<3;i++)free_page(NULL,p->pages[i].frame);
    assert(active_pages==0);
    (void)original;

    assert(stage(&table,&pid)==NOVA_PROCESS_OK);
    p=nova_process_find(&table,pid);
    assert(p);
    uint8_t snapshot[4096];
    memcpy(snapshot,p->pages[0].frame,sizeof(snapshot));
    p->pages[0].physical_page=p->pages[1].physical_page;
    uint32_t rejected=99;
    assert(stage(&table,&rejected)==NOVA_PROCESS_BAD_FRAME);
    assert(rejected==0 && active_pages==4);
    assert(memcmp(snapshot,p->pages[0].frame,sizeof(snapshot))==0);
    free_page(NULL,frames[3]); /* fresh, rejected and deliberately leaked */
    p->pages[0].physical_page=UINT64_C(0x200000);
    assert(nova_process_drop(&table,pid,free_page,physical_page,NULL));
    assert(active_pages==0);
}
int main(void){
    test_staging();
    test_failures();
    test_borrowed_frame_rejected_before_write();
    test_corrupted_image_teardown_fails_closed();
    test_physical_alias_rejected_before_copy();
    test_unverifiable_physical_frame_fails_closed();
    test_physical_identity_rechecked_on_drop_and_stage();
    assert(active_pages==0);
    puts("Staged process images: validated ELF, zeroed BSS, separate frames, rollback, limits and release OK.");
    return 0;
}
