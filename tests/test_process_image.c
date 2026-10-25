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
static uint8_t frames[TEST_POOL][4096];
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
static enum nova_process_result stage(struct nova_process_table *table,
                                      uint32_t *pid){
    return nova_process_stage(table,"DISK:/APP.ELF",read_image,NULL,IMAGE_SIZE,
                              alloc_page,free_page,NULL,pid);
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
    assert(nova_process_drop(&tab,pid,free_page,NULL));
    assert(active_pages==0 && nova_process_count(&tab)==0);
    assert(!nova_process_drop(&tab,pid,free_page,NULL));
    assert(!nova_process_find(&tab,pid));
    assert(!strcmp(nova_process_result_name(NOVA_PROCESS_OK),
                   "IMAGE STAGED - NOT EXECUTABLE YET"));
    assert(!strcmp(nova_elf_result_name(NOVA_ELF_OK),
                   "VALID STATIC ELF64 (NOT EXECUTED)"));
}
static void test_failures(void){
    fixture();struct nova_process_table tab={0};uint32_t pid=88;
    assert(nova_process_stage(NULL,"APP",read_image,NULL,IMAGE_SIZE,
              alloc_page,free_page,NULL,&pid)==NOVA_PROCESS_BAD_INPUT);
    assert(pid==0);
    assert(nova_process_stage(&tab,"APP",read_image,NULL,IMAGE_SIZE,
              alloc_page,free_page,NULL,NULL)==NOVA_PROCESS_BAD_INPUT);
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
        assert(nova_process_drop(&tab,ids[i],free_page,NULL));
    assert(active_pages==0&&nova_process_count(&tab)==0);
    assert(stage(&tab,&pid)==NOVA_PROCESS_OK&&pid==5); /* monotonic IDs */
    assert(nova_process_drop(&tab,pid,free_page,NULL));
}
int main(void){
    test_staging();
    test_failures();
    assert(active_pages==0);
    puts("Staged process images: validated ELF, zeroed BSS, separate frames, rollback, limits and release OK.");
    return 0;
}
