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
#include "../kernel/process_image.h"
#include "../kernel/capabilities.h"
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
/* Synthetic physical-frame allocator for the end-to-end stage test. */
static _Alignas(4096) uint8_t staged_frames[4][4096];
static bool staged_used[4];
static unsigned staged_count=0;
static void *stage_alloc(void *ctx){
    (void)ctx;
    for(unsigned i=0;i<4;i++)if(!staged_used[i]){
        staged_used[i]=true;staged_count++;
        memset(staged_frames[i],0xBF,4096);
        return staged_frames[i];
    }
    return NULL;
}
static void stage_free(void *ctx,void *page){
    (void)ctx;
    for(unsigned i=0;i<4;i++)if(page==staged_frames[i]){
        assert(staged_used[i]);
        staged_used[i]=false;staged_count--;return;
    }
    assert(!"invalid test frame");
}

static bool stage_physical(void *ctx,const void *page,uint64_t *out){
    (void)ctx;
    if(!out)return false;
    for(unsigned i=0;i<4;i++)if(page==staged_frames[i] && staged_used[i]){
        *out=UINT64_C(0x300000)+(uint64_t)i*4096u;
        return true;
    }
    return false;
}

int main(void){
    assert(nova_ramfs_self_test());
    image_fixture();
    struct nova_fat16 fat16;
    assert(nova_fat16_mount(&fat16,read_sector,NULL,DISK_SECTORS));
    struct nova_vfs vfs={.disk=&fat16};
    struct nova_vfs_entry entry;
    assert(nova_vfs_list(&vfs,"DISK:/",0,&entry));
    assert(!strcmp(entry.name,"APP.ELF")&&entry.read_only);
    assert(nova_vfs_create(&vfs,"RAM:/SCRATCH.TXT"));
    assert(nova_vfs_remove(&vfs,"RAM:/SCRATCH.TXT"));
    struct elf_vfs_ctx ctx={.vfs=&vfs};
    uint32_t file_bytes=0;bool read_only=false;
    assert(nova_vfs_size(&vfs,"DISK:/APP.ELF",&file_bytes,&read_only));
    assert(file_bytes==ELF_FILE_BYTES&&read_only);
    struct nova_elf_info info;
    assert(nova_elf64_probe(read_elf_vfs,&ctx,file_bytes,&info)==NOVA_ELF_OK);
    assert(info.entry==0x401000&&info.load_count==2&&info.pages==2);
    assert(!strcmp(nova_elf_result_name(NOVA_ELF_OK),"VALID STATIC ELF64 (NOT EXECUTED)"));
    assert(!nova_vfs_write(&vfs,"DISK:/APP.ELF",(const uint8_t*)"X",1));
    /* ELF -> FAT16 -> VFS -> isolated physical frames; no instruction execution. */
    struct nova_process_table table={0};
    uint32_t pid=0;
    assert(nova_process_stage(&table,"DISK:/APP.ELF",
        read_elf_vfs,&ctx,file_bytes,stage_alloc,stage_free,stage_physical,NULL,&pid)==NOVA_PROCESS_OK);
    assert(pid==1 && staged_count==2 && nova_process_count(&table)==1);
    struct nova_process_image *process=nova_process_find(&table,pid);
    assert(process && process->entry==0x401000 && process->page_count==2);
    uint8_t fetched[12];
    assert(nova_process_peek(process,0x402080,fetched,sizeof(fetched)));
    for(size_t i=0;i<sizeof(fetched);i++)assert(fetched[i]==0); /* zeroed BSS */
    /* Staged programs start with NO rights: no file reads or writes. */
    struct nova_cap_registry policy={0};
    assert(nova_cap_register(&policy,&table,pid));
    assert(!nova_cap_register(&policy,&table,pid));
    assert(nova_cap_current(&policy,&table,pid)==0);
    assert(!nova_cap_read_file(&policy,&table,pid,&vfs,"DISK:/APP.ELF",0,fetched,8));
    assert(!nova_cap_read_file(&policy,&table,pid,&vfs,"RAM:/NOTES.TXT",0,fetched,8));
    assert(!nova_cap_write_ram(&policy,&table,pid,&vfs,"RAM:/NOTES.TXT",
                               (const uint8_t*)"UNSAFE",6));
    assert(!nova_cap_check(&policy,&table,pid,NOVA_CAP_RAM_READ));
    assert(!nova_cap_check(&policy,&table,pid,0));
    assert(!nova_cap_check(&policy,&table,pid,NOVA_CAP_NETWORK));
    assert(!nova_cap_grant_trusted(&policy,&table,pid,NOVA_CAP_NETWORK));
    assert(!nova_cap_grant_trusted(&policy,&table,pid,NOVA_CAP_MICROPHONE));
    assert(!nova_cap_grant_trusted(&policy,&table,pid,NOVA_CAP_CONTROLLER));
    assert(!nova_cap_grant_trusted(&policy,&table,pid,NOVA_CAP_RAM_READ|NOVA_CAP_NETWORK));
    assert(!nova_cap_grant_trusted(&policy,&table,pid,0));

    /* A trusted future kernel broker can choose an explicit read grant.
       Still no syscalls or usermode program execution exist. */
    assert(nova_cap_grant_trusted(&policy,&table,pid,NOVA_CAP_DISK_READ));
    assert(nova_cap_current(&policy,&table,pid)==NOVA_CAP_DISK_READ);
    assert(nova_cap_read_file(&policy,&table,pid,&vfs,"DISK:/APP.ELF",0,fetched,4));
    assert(fetched[0]==0x7f&&fetched[1]=='E'&&fetched[2]=='L'&&fetched[3]=='F');
    assert(!nova_cap_read_file(&policy,&table,pid,&vfs,"RAM:/NOTES.TXT",0,fetched,4));
    assert(!nova_cap_write_ram(&policy,&table,pid,&vfs,"DISK:/APP.ELF",
                               (const uint8_t*)"X",1));
    assert(!nova_cap_write_ram(&policy,&table,pid,&vfs,"RAM:/NOTES.TXT",
                               (const uint8_t*)"X",1));
    assert(nova_cap_grant_trusted(&policy,&table,pid,
           NOVA_CAP_RAM_READ|NOVA_CAP_RAM_WRITE));
    assert(nova_cap_read_file(&policy,&table,pid,&vfs,"RAM:/NOTES.TXT",0,fetched,5));
    assert(!nova_cap_write_ram(&policy,&table,pid,&vfs,"RAM:/NOTES.TXT",
                               (const uint8_t*)"X",1)); /* protected RAM sample */
    assert(nova_vfs_create(&vfs,"RAM:/GAME.TXT")); /* privileged setup */
    assert(nova_cap_write_ram(&policy,&table,pid,&vfs,"RAM:/GAME.TXT",
                               (const uint8_t*)"SAVE",4));
    assert(nova_cap_read_file(&policy,&table,pid,&vfs,"RAM:/GAME.TXT",0,fetched,4));
    assert(!memcmp(fetched,"SAVE",4));
    assert(!nova_cap_read_file(&policy,&table,pid,&vfs,"RAM:/../NOTES.TXT",0,fetched,1));
    assert(!nova_cap_write_ram(&policy,&table,pid,&vfs,"DISK:/APP.ELF",
                               (const uint8_t*)"D",1));
    assert(nova_cap_revoke_trusted(&policy,&table,pid,NOVA_CAP_RAM_WRITE));
    assert(!nova_cap_write_ram(&policy,&table,pid,&vfs,"RAM:/GAME.TXT",
                                (const uint8_t*)"NO",2));
    assert(nova_cap_revoke_trusted(&policy,&table,pid,NOVA_CAP_DISK_READ));
    assert(!nova_cap_read_file(&policy,&table,pid,&vfs,"DISK:/APP.ELF",0,fetched,4));
    assert(!nova_cap_revoke_trusted(&policy,&table,pid,NOVA_CAP_MICROPHONE));
    assert(nova_vfs_remove(&vfs,"RAM:/GAME.TXT"));
    assert(nova_cap_unregister(&policy,pid));
    assert(!nova_cap_unregister(&policy,pid));
    assert(!nova_cap_read_file(&policy,&table,pid,&vfs,"RAM:/NOTES.TXT",0,fetched,1));
    assert(nova_process_drop(&table,pid,stage_free,stage_physical,NULL));
    assert(!nova_cap_grant_trusted(&policy,&table,pid,NOVA_CAP_RAM_READ));
    assert(staged_count==0 && nova_process_count(&table)==0);
    /* A stale policy slot must not grant rights after process teardown or
       accidentally transfer rights to the next monotonically numbered PID. */
    uint32_t stale=0;
    assert(nova_process_stage(&table,"DISK:/APP.ELF",
        read_elf_vfs,&ctx,file_bytes,stage_alloc,stage_free,stage_physical,NULL,&stale)==NOVA_PROCESS_OK);
    assert(stale!=pid);
    assert(nova_cap_register(&policy,&table,stale));
    assert(nova_cap_grant_trusted(&policy,&table,stale,NOVA_CAP_RAM_READ));
    assert(nova_process_drop(&table,stale,stage_free,stage_physical,NULL));
    assert(!nova_cap_check(&policy,&table,stale,NOVA_CAP_RAM_READ));
    assert(!nova_cap_grant_trusted(&policy,&table,stale,NOVA_CAP_DISK_READ));
    assert(nova_cap_unregister(&policy,stale));
    uint32_t fresh=0;
    assert(nova_process_stage(&table,"DISK:/APP.ELF",
        read_elf_vfs,&ctx,file_bytes,stage_alloc,stage_free,stage_physical,NULL,&fresh)==NOVA_PROCESS_OK);
    assert(fresh!=stale && fresh!=pid);
    assert(nova_cap_register(&policy,&table,fresh));
    assert(nova_cap_current(&policy,&table,fresh)==0);
    assert(!nova_cap_read_file(&policy,&table,fresh,&vfs,
                               "DISK:/APP.ELF",0,fetched,4));
    assert(nova_cap_unregister(&policy,fresh));
    assert(nova_process_drop(&table,fresh,stage_free,stage_physical,NULL));
    assert(staged_count==0);
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
    puts("ELF64 + FAT16 + VFS + staged frames + default-deny capability mediation: passed.");
    return 0;
}
