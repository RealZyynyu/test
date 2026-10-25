/* Host-side FAT16 unit tests: no QEMU, ISO, or physical disk access.
 * Builds a synthetic 8 MiB disk in memory; validates read-only parsing.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/fat16.h"
#include "../kernel/vfs.h"
#define SECTORS 16384u
static uint8_t disk[SECTORS][512];
static unsigned read_count=0;
static bool memory_reader(void *context,uint32_t lba,uint8_t out[512]){
    (void)context;
    if(lba>=SECTORS)return false;
    memcpy(out,disk[lba],512);
    read_count++;
    return true;
}
static void w16(uint8_t *d,unsigned o,uint16_t v){
    d[o]=(uint8_t)v;d[o+1]=(uint8_t)(v>>8);
}
static void w32(uint8_t *d,unsigned o,uint32_t v){
    d[o]=(uint8_t)v;d[o+1]=(uint8_t)(v>>8);
}
static void fixture(uint32_t start){
    memset(disk,0,sizeof(disk));
    uint32_t length=SECTORS-start;
    if(start){
        uint8_t *mbr=disk[0];
        mbr[446+4]=0x06; /* FAT16 primary partition */
        w32(mbr,446+8,start);
        w32(mbr,446+12,length);
        mbr[510]=0x55;mbr[511]=0xAA;
    }
    uint8_t *bpb=disk[start];
    bpb[0]=0xEB;bpb[1]=0x3C;bpb[2]=0x90;
    w16(bpb,11,512);          /* 512 bytes per sector */
    bpb[13]=1;                /* sectors per cluster */
    w16(bpb,14,1);           /* reserved */
    bpb[16]=1;                /* one FAT */
    w16(bpb,17,32);          /* root directory entries */
    w16(bpb,19,(uint16_t)length);
    bpb[21]=0xF8;
    w16(bpb,22,64);          /* 64 FAT sectors */
    bpb[510]=0x55;bpb[511]=0xAA;

    /* FAT root and data offsets: start+65 root, start+67 data. */
    uint8_t *fat=disk[start+1];
    w16(fat,0,0xFFF8);w16(fat,2,0xFFFF);
    w16(fat,4,3);w16(fat,6,0xFFFF);w16(fat,8,0xFFFF);

    uint8_t *root=disk[start+65];
    memcpy(root,"HELLO   TXT",11);
    root[11]=0x20;
    w16(root,26,2);
    w32(root,28,600);
    uint8_t *second=root+32;
    memcpy(second,"SMALL   TXT",11);
    second[11]=0x20;
    w16(second,26,4);
    w32(second,28,5);

    memset(disk[start+67],'A',512);
    memset(disk[start+68],'B',512);
    memcpy(disk[start+69],"HELLO",5);
}
static void verify(uint32_t partition_start){
    fixture(partition_start);
    struct nova_fat16 vol;
    assert(nova_fat16_mount(&vol,memory_reader,NULL,SECTORS));
    assert(vol.first_sector==partition_start);
    assert(vol.sectors_per_cluster==1);
    assert(vol.cluster_count>=4085);
    struct nova_fat_file file,small;
    assert(nova_fat16_entry(&vol,0,&file));
    assert(!strcmp(file.name,"HELLO.TXT")&&file.size==600);
    assert(nova_fat16_entry(&vol,1,&small));
    assert(!strcmp(small.name,"SMALL.TXT")&&small.size==5);
    assert(!nova_fat16_entry(&vol,2,&small));
    assert(nova_fat16_find(&vol,"HELLO.TXT",&file));
    assert(!nova_fat16_find(&vol,"HELLX.TXT",&small));  /* same length, different character */
    assert(nova_fat16_find(&vol,"SMALL.TXT",&small));
    uint8_t contents[600];
    memset(contents,0,sizeof(contents));
    assert(nova_fat16_read(&vol,&file,0,contents,600));
    for(unsigned i=0;i<512;i++)assert(contents[i]=='A');
    for(unsigned i=512;i<600;i++)assert(contents[i]=='B');
    assert(nova_fat16_read(&vol,&file,510,contents,10));
    assert(contents[0]=='A'&&contents[1]=='A'&&contents[2]=='B');
    assert(nova_fat16_read(&vol,&small,0,contents,5));
    assert(!memcmp(contents,"HELLO",5));
    assert(!nova_fat16_read(&vol,&small,1,contents,5));
    assert(!nova_fat16_read(&vol,&small,6,contents,0));
    assert(nova_fat16_read(&vol,&small,5,contents,0));
    assert(!nova_fat16_read(&vol,&file,0,NULL,8));
    assert(nova_fat16_read(&vol,&file,0,NULL,0));

    /* File chain stops prematurely: reading 600 bytes must fail. */
    w16(disk[partition_start+1],4,0xFFFF);
    assert(!nova_fat16_read(&vol,&file,0,contents,600));
    w16(disk[partition_start+1],4,3);

    /* Malformed BPB and volume signature are rejected. */
    uint8_t *boot=disk[partition_start];
    boot[13]=3; assert(!nova_fat16_mount(&vol,memory_reader,NULL,SECTORS));
    boot[13]=1;
    boot[510]=0;assert(!nova_fat16_mount(&vol,memory_reader,NULL,SECTORS));
}

static void verify_vfs(void){
    assert(nova_ramfs_self_test());
    fixture(0);
    struct nova_fat16 diskfs;
    assert(nova_fat16_mount(&diskfs,memory_reader,NULL,SECTORS));
    struct nova_vfs vfs={.disk=&diskfs};
    struct nova_vfs_entry entry;
    uint32_t size=0;bool ro=false;

    assert(nova_vfs_list(&vfs,"RAM:/",0,&entry));
    assert(!strcmp(entry.name,"NOTES.TXT")&&entry.read_only);
    assert(nova_vfs_list(&vfs,"DISK:/",0,&entry));
    assert(!strcmp(entry.name,"HELLO.TXT")&&entry.size==600&&entry.read_only);
    assert(nova_vfs_list(&vfs,"DISK:/",1,&entry));
    assert(!strcmp(entry.name,"SMALL.TXT")&&entry.size==5);
    assert(!nova_vfs_list(&vfs,"DISK:/",2,&entry));
    assert(!nova_vfs_list(&vfs,"DISK:/HELLO.TXT",0,&entry));

    assert(nova_vfs_size(&vfs,"DISK:/HELLO.TXT",&size,&ro));
    assert(size==600&&ro);
    uint8_t contents[16]={0};
    assert(nova_vfs_read(&vfs,"DISK:/HELLO.TXT",510,contents,10));
    assert(contents[0]=='A'&&contents[1]=='A'&&contents[2]=='B');
    assert(!nova_vfs_read(&vfs,"DISK:/HELLO.TXT",595,contents,10));
    assert(!nova_vfs_size(&vfs,"DISK:/MISS.TXT",&size,&ro));

    assert(nova_vfs_create(&vfs,"RAM:/WORK.TXT"));
    assert(nova_vfs_write(&vfs,"RAM:/WORK.TXT",(const uint8_t*)"NOVA",4));
    assert(nova_vfs_size(&vfs,"RAM:/WORK.TXT",&size,&ro));
    assert(size==4&&!ro);
    memset(contents,0,sizeof(contents));
    assert(nova_vfs_read(&vfs,"RAM:/WORK.TXT",0,contents,4));
    assert(!memcmp(contents,"NOVA",4));
    assert(!nova_vfs_write(&vfs,"RAM:/WORK.TXT",(const uint8_t*)"X",128));
    assert(nova_vfs_remove(&vfs,"RAM:/WORK.TXT"));
    assert(!nova_vfs_size(&vfs,"RAM:/WORK.TXT",&size,&ro));
    assert(!nova_vfs_remove(&vfs,"RAM:/NOTES.TXT")); /* protected system file */

    /* Hard read-only boundary: not even the API offers disk writes. */
    assert(!nova_vfs_create(&vfs,"DISK:/BAD.TXT"));
    assert(!nova_vfs_write(&vfs,"DISK:/SMALL.TXT",(const uint8_t*)"X",1));
    assert(!nova_vfs_remove(&vfs,"DISK:/SMALL.TXT"));
    assert(!nova_vfs_create(&vfs,"RAM:/../../BAD"));
    assert(!nova_vfs_create(&vfs,"RAM:/SPACE BAD"));
    assert(!nova_vfs_create(&vfs,"RAM:/TOO-LONG-FILENAME.TXT"));
    assert(!nova_vfs_read(&vfs,"DISK:/../HELLO.TXT",0,contents,2));
    assert(!nova_vfs_read(&vfs,"RAM:/NONEXIST",0,contents,2));
    assert(!nova_vfs_read(&vfs,"FILE:/HELLO.TXT",0,contents,2));
    assert(!nova_vfs_read(&vfs,"DISK:/HELLO.TXT",(size_t)-1,contents,1));
    assert(!nova_vfs_read(&vfs,"DISK:/HELLO.TXT",0,NULL,1));
    assert(nova_vfs_read(&vfs,"DISK:/HELLO.TXT",600,NULL,0));

    struct nova_vfs no_disk={.disk=NULL};
    assert(!nova_vfs_list(&no_disk,"DISK:/",0,&entry));
    assert(!nova_vfs_read(&no_disk,"DISK:/HELLO.TXT",0,contents,2));
    assert(nova_vfs_list(&no_disk,"RAM:/",0,&entry));
}

int main(void){
    verify(0);   /* superfloppy */
    verify(1);   /* partitioned MBR */
    verify_vfs();
    assert(read_count>0);
    puts("FAT16 and VFS tests passed (read-only disk, RAM writes, path validation, cluster bounds, MBR)");
    return 0;
}
