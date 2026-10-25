/* End-to-end source-only test: inspect a synthetic ELF64 from a read-only
 * FAT16 volume through NOVA's VFS, without executing it or building an ISO.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/vfs.h"
#include "../kernel/elf64.h"
#define DISK_SECTORS 16384u
#define ELF_FILE_BYTES 8320u
#define DATA_SECTOR 67u
static uint8_t sectors[DISK_SECTORS][512];
static bool read_sector(void *context,uint32_t sector,uint8_t out[512]){
    (void)context;
    if(sector>=DISK_SECTORS)return false;
    memcpy(out,sectors[sector],512);
    return true;
}
static void wr16(uint8_t *p,uint16_t n){p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static void wr32(uint8_t *p,uint32_t n){
    for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));
}
static void wr64(uint8_t *p,uint64_t n){
    for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(n>>(8*i));
}
static uint8_t *elf_byte(uint32_t position){
    return &sectors[DATA_SECTOR+position/512u][position%512u];
}
static void image_fixture(void){
    memset(sectors,0,sizeof(sectors));
    uint8_t *bpb=sectors[0];
    bpb[0]=0xEB;bpb[1]=0x3C;bpb[2]=0x90;
    wr16(bpb+11,512);bpb[13]=1;wr16(bpb+14,1);
    bpb[16]=1;wr16(bpb+17,32);wr16(bpb+19,DISK_SECTORS);
    bpb[21]=0xF8;wr16(bpb+22,64);
    bpb[510]=0x55;bpb[511]=0xAA;
    uint8_t *fat=sectors[1];
    wr16(fat,0xFFF8);wr16(fat+2,0xFFFF);
    const uint32_t needed=(ELF_FILE_BYTES+511u)/512u;
    for(uint32_t i=0;i<needed;i++){
        uint16_t cluster=(uint16_t)(i+2);
        uint16_t next=(uint16_t)(i+1==needed?0xFFFF:cluster+1);
        wr16(fat+2u*cluster,next);
    }
    uint8_t *root=sectors[65];
    memcpy(root,"APP     ELF",11);
    root[11]=0x20;wr16(root+26,2);wr32(root+28,ELF_FILE_BYTES);
    *elf_byte(0)=0x7f;*elf_byte(1)='E';*elf_byte(2)='L';
    *elf_byte(3)='F';*elf_byte(4)=2;*elf_byte(5)=1;*elf_byte(6)=1;
    wr16(elf_byte(16),2);wr16(elf_byte(18),62);wr32(elf_byte(20),1);
    wr64(elf_byte(24),0x401000);wr64(elf_byte(32),64);
    wr16(elf_byte(52),64);wr16(elf_byte(54),56);wr16(elf_byte(56),2);
    uint8_t *ph=elf_byte(64);
    wr32(ph,1);wr32(ph+4,5);
    wr64(ph+8,4096);wr64(ph+16,0x401000);
    wr64(ph+32,128);wr64(ph+40,4096);wr64(ph+48,4096);
    ph=elf_byte(120);
    wr32(ph,1);wr32(ph+4,6);
    wr64(ph+8,8192);wr64(ph+16,0x402000);
    wr64(ph+32,128);wr64(ph+40,4096);wr64(ph+48,4096);
}
struct elf_vfs_ctx {struct nova_vfs *vfs;};
static bool read_elf_vfs(void *context,uint64_t offset,uint8_t *out,size_t length){
    struct elf_vfs_ctx *x=(struct elf_vfs_ctx*)context;
    if(offset>SIZE_MAX)return false;
    return nova_vfs_read(x->vfs,"DISK:/APP.ELF",(size_t)offset,out,length);
}
int main(void){
    assert(nova_ramfs_self_test());
    image_fixture();
    struct nova_fat16 fat16;
    assert(nova_fat16_mount(&fat16,read_sector,NULL,DISK_SECTORS));
    struct nova_vfs vfs={.disk=&fat16};
    struct elf_vfs_ctx ctx={.vfs=&vfs};
    uint32_t file_bytes=0;bool read_only=false;
    assert(nova_vfs_size(&vfs,"DISK:/APP.ELF",&file_bytes,&read_only));
    assert(file_bytes==ELF_FILE_BYTES&&read_only);
    struct nova_elf_info info;
    assert(nova_elf64_probe(read_elf_vfs,&ctx,file_bytes,&info)==NOVA_ELF_OK);
    assert(info.entry==0x401000&&info.load_count==2&&info.pages==2);
    assert(!nova_vfs_write(&vfs,"DISK:/APP.ELF",(const uint8_t*)"X",1));
    /* File metadata can be located, but disk modification remains forbidden. */
    uint8_t *second_ph=elf_byte(120);
    uint32_t old_flags=nova_elf_u32(second_ph+4);
    wr32(second_ph+4,7); /* W + X segment is unsafe */
    assert(nova_elf64_probe(read_elf_vfs,&ctx,file_bytes,&info)==NOVA_ELF_UNSAFE);
    wr32(second_ph+4,old_flags);
    /* File's FAT chain is broken. Program headers are still readable. */
    wr16(sectors[1]+4,0xFFF7);
    assert(!nova_vfs_read(&vfs,"DISK:/APP.ELF",4096,(uint8_t*)&info,1));
    /* Missing/unmounted disk also fails closed. */
    struct nova_vfs empty={.disk=NULL};
    struct elf_vfs_ctx no_disk={.vfs=&empty};
    assert(nova_elf64_probe(read_elf_vfs,&no_disk,file_bytes,&info)==NOVA_ELF_IO_ERROR);
    puts("ELF64 + FAT16 + VFS: end-to-end read-only program inspection tests passed.");
    return 0;
}
