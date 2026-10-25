/* Host-only tests of NOVA's ELF64 validator. No program is executed. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/elf64.h"
#define TEST_IMAGE_SIZE 16384u
static uint8_t image[TEST_IMAGE_SIZE];
static bool inject_io_error=false;
static bool mem_reader(void *context,uint64_t offset,uint8_t *out,size_t n){
    (void)context;
    if(inject_io_error||offset>TEST_IMAGE_SIZE||n>TEST_IMAGE_SIZE-offset)return false;
    memcpy(out,image+(size_t)offset,n);
    return true;
}
static void u16(size_t pos,uint16_t v){
    image[pos]=(uint8_t)v;image[pos+1]=(uint8_t)(v>>8);
}
static void u32(size_t pos,uint32_t v){
    for(unsigned i=0;i<4;i++)image[pos+i]=(uint8_t)(v>>(i*8));
}
static void u64(size_t pos,uint64_t v){
    for(unsigned i=0;i<8;i++)image[pos+i]=(uint8_t)(v>>(i*8));
}
static void ph(unsigned index,uint32_t type,uint32_t flags,
               uint64_t offset,uint64_t vaddr,
               uint64_t filesz,uint64_t memsz,uint64_t align){
    size_t p=64u+(size_t)index*56u;
    u32(p,type);u32(p+4,flags);u64(p+8,offset);
    u64(p+16,vaddr);u64(p+24,0);
    u64(p+32,filesz);u64(p+40,memsz);u64(p+48,align);
}
static void good_image(void){
    memset(image,0,sizeof(image));
    image[0]=0x7f;image[1]='E';image[2]='L';image[3]='F';
    image[4]=2;image[5]=1;image[6]=1;
    u16(16,2);u16(18,62);u32(20,1);
    u64(24,0x401000);u64(32,64);
    u16(52,64);u16(54,56);u16(56,2);
    ph(0,1,5,0x1000,0x401000,128,4096,4096); /* RX text */
    ph(1,1,6,0x2000,0x402000,128,4096,4096); /* RW data */
}
static enum nova_elf_result inspect(void){
    struct nova_elf_info out;
    return nova_elf64_probe(mem_reader,NULL,TEST_IMAGE_SIZE,&out);
}
static void test_success(void){
    good_image();
    struct nova_elf_info out;
    assert(nova_elf64_probe(mem_reader,NULL,TEST_IMAGE_SIZE,&out)==NOVA_ELF_OK);
    assert(out.entry==0x401000&&out.load_count==2&&out.pages==2);
    assert(out.loads[0].file_offset==0x1000&&out.loads[0].flags==5);
    assert(out.loads[1].virtual_address==0x402000&&out.loads[1].memory_size==4096);
    assert(!strcmp(nova_elf_result_name(NOVA_ELF_OK),"VALID STATIC ELF64 (NOT EXECUTED)"));
}
static void test_bad_headers(void){
    good_image();image[1]='X';assert(inspect()==NOVA_ELF_BAD_FORMAT);
    good_image();image[4]=1;assert(inspect()==NOVA_ELF_UNSUPPORTED);
    good_image();image[5]=2;assert(inspect()==NOVA_ELF_UNSUPPORTED);
    good_image();image[7]=7;assert(inspect()==NOVA_ELF_UNSUPPORTED);
    good_image();u16(16,3);assert(inspect()==NOVA_ELF_UNSUPPORTED);/* PIE / ET_DYN */
    good_image();u16(18,3);assert(inspect()==NOVA_ELF_UNSUPPORTED);
    good_image();u32(20,2);assert(inspect()==NOVA_ELF_BAD_FORMAT);
    good_image();u16(52,65);assert(inspect()==NOVA_ELF_BAD_FORMAT);
    good_image();u16(54,64);assert(inspect()==NOVA_ELF_BAD_FORMAT);
    good_image();u16(56,0);assert(inspect()==NOVA_ELF_UNSUPPORTED);
    good_image();u16(56,65);assert(inspect()==NOVA_ELF_UNSUPPORTED);
    good_image();u64(32,TEST_IMAGE_SIZE-20u);assert(inspect()==NOVA_ELF_BAD_FORMAT);
    good_image();u64(32,UINT64_MAX);assert(inspect()==NOVA_ELF_BAD_FORMAT);
    good_image();
    struct nova_elf_info out;
    assert(nova_elf64_probe(mem_reader,NULL,63,&out)==NOVA_ELF_BAD_FORMAT);
    assert(nova_elf64_probe(mem_reader,NULL,TEST_IMAGE_SIZE,NULL)==NOVA_ELF_BAD_FORMAT);
    inject_io_error=true;assert(inspect()==NOVA_ELF_IO_ERROR);inject_io_error=false;
}
static void test_layout(void){
    good_image();u64(24,0x403000);assert(inspect()==NOVA_ELF_UNSAFE);
    good_image();u64(24,0x401080);assert(inspect()==NOVA_ELF_UNSAFE); /* entry in BSS */
    good_image();u64(24,0xFFFF800000100000ull);assert(inspect()==NOVA_ELF_UNSAFE);
    good_image();u64(24,0x1000);assert(inspect()==NOVA_ELF_UNSAFE);
    good_image();u32(64+4,7);assert(inspect()==NOVA_ELF_UNSAFE); /* W+X */
    good_image();ph(1,1,6,0x2000,0x401800,128,4096,4096);
    assert(inspect()==NOVA_ELF_UNSAFE); /* overlap, even sub-page */
    good_image();ph(1,1,6,0x2000,0x401000,128,4096,4096);
    assert(inspect()==NOVA_ELF_UNSAFE);
    good_image();u64(64+56+48,3);assert(inspect()==NOVA_ELF_UNSAFE);
    good_image();u64(64+56+48,16);assert(inspect()==NOVA_ELF_UNSAFE);
    good_image();u64(64+56+16,0x402001);assert(inspect()==NOVA_ELF_UNSAFE);
    good_image();u64(64+56+16,0x00007fff00000000ull);
    assert(inspect()==NOVA_ELF_BAD_FORMAT);
    good_image();u64(64+56+40,0);assert(inspect()==NOVA_ELF_BAD_FORMAT);
    good_image();u64(64+56+40,16);assert(inspect()==NOVA_ELF_BAD_FORMAT);
    good_image();u64(64+56+32,UINT64_MAX);assert(inspect()==NOVA_ELF_BAD_FORMAT);
    good_image();u64(64+56+8,UINT64_MAX);assert(inspect()==NOVA_ELF_BAD_FORMAT);
    good_image();u64(64+56+40,4096ull*4097ull);
    assert(inspect()==NOVA_ELF_UNSAFE); /* exceeds 4096-page cap */
    good_image();u32(64+56,3);assert(inspect()==NOVA_ELF_UNSUPPORTED); /* PT_INTERP */
    good_image();u32(64+56,2);assert(inspect()==NOVA_ELF_UNSUPPORTED); /* DYNAMIC */
    good_image();u32(64+56,7);assert(inspect()==NOVA_ELF_UNSUPPORTED); /* TLS */
    good_image();u32(64+56,0x6474e551u);u32(64+56+4,7);
    assert(inspect()==NOVA_ELF_UNSAFE); /* executable stack */
    good_image();u32(64+56,0x6474e551u);u32(64+56+4,6);
    assert(inspect()==NOVA_ELF_OK); /* non-executable stack metadata */
    good_image();u32(64,0);assert(inspect()==NOVA_ELF_UNSAFE); /* entry no executable seg */
    good_image();u16(56,1);assert(inspect()==NOVA_ELF_OK); /* standalone RX segment */
}
int main(void){
    test_success();test_bad_headers();test_layout();
    puts("ELF64 validator: all tests passed (x86_64, static segments, W^X, canonical ranges, truncation, malicious headers).");
    return 0;
}
