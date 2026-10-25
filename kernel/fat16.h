/* NOVA FAT16 read-only filesystem.
 * Supports FAT16 volumes in a primary MBR partition or "superfloppy" at LBA 0,
 * root-directory 8.3 files, and bounded reads following FAT cluster chains.
 * No writes, formatting, mounting host drives, long file names, subdirectories,
 * FAT12/FAT32, partition editing or disk installation support.
 * All physical I/O goes through a caller-supplied READ-ONLY sector callback.
 */
#ifndef NOVA_FAT16_H
#define NOVA_FAT16_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef bool (*nova_sector_read_fn)(void *context,uint32_t lba,uint8_t out[512]);

struct nova_fat16 {
    nova_sector_read_fn read;
    void *context;
    uint32_t device_sectors;
    uint32_t first_sector;
    uint32_t volume_sectors;
    uint32_t fat_sector,root_sector,data_sector;
    uint32_t root_sectors;
    uint32_t cluster_count;
    uint16_t fat_sectors,root_entries;
    uint8_t sectors_per_cluster;
    bool mounted;
};
struct nova_fat_file {
    char name[13];   /* NUL-terminated 8.3 name */
    uint16_t first_cluster;
    uint32_t size;
};

static uint16_t nova_fat_u16(const uint8_t *p){
    return (uint16_t)((uint16_t)p[0]|((uint16_t)p[1]<<8));
}
static uint32_t nova_fat_u32(const uint8_t *p){
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static bool nova_fat_disk_read(const struct nova_fat16 *fs,uint32_t lba,uint8_t buf[512]){
    if(!fs->read||lba>=fs->device_sectors)return false;
    return fs->read(fs->context,lba,buf);
}
static bool nova_fat_mount_at(struct nova_fat16 *fs,uint32_t start,uint32_t allowed){
    uint8_t bpb[512];
    if(start>=fs->device_sectors||allowed==0)return false;
    if(!nova_fat_disk_read(fs,start,bpb))return false;
    if(bpb[510]!=0x55||bpb[511]!=0xAA)return false;
    if(nova_fat_u16(bpb+11)!=512)return false;
    uint8_t spc=bpb[13],fats=bpb[16];
    uint16_t reserved=nova_fat_u16(bpb+14);
    uint16_t roots=nova_fat_u16(bpb+17);
    uint16_t fat_sectors=nova_fat_u16(bpb+22);
    uint32_t total=nova_fat_u16(bpb+19);
    if(!total)total=nova_fat_u32(bpb+32);
    if(!spc||(spc&(spc-1))||spc>128||!reserved||!fat_sectors||
       !roots||fats<1||fats>2||!total||total>allowed)return false;
    uint32_t root_secs=((uint32_t)roots*32u+511u)/512u;
    uint64_t nondata=(uint64_t)reserved+(uint64_t)fats*fat_sectors+root_secs;
    if(nondata>=total)return false;
    uint32_t clusters=(uint32_t)((total-nondata)/spc);
    if(clusters<4085||clusters>=65525)return false; /* FAT16 geometry only */
    if((uint64_t)fat_sectors*256u<((uint64_t)clusters+2u))return false;
    uint64_t end=(uint64_t)start+total;
    if(end>fs->device_sectors)return false;
    fs->first_sector=start;fs->volume_sectors=total;
    fs->fat_sector=start+reserved;
    fs->root_sector=(uint32_t)((uint64_t)fs->fat_sector+(uint64_t)fats*fat_sectors);
    fs->data_sector=fs->root_sector+root_secs;
    fs->root_sectors=root_secs;fs->cluster_count=clusters;
    fs->sectors_per_cluster=spc;fs->fat_sectors=fat_sectors;
    fs->root_entries=roots;
    fs->mounted=true;
    return true;
}
static bool nova_fat16_mount(struct nova_fat16 *fs,nova_sector_read_fn reader,
                             void *context,uint32_t device_sectors){
    if(!fs||!reader||device_sectors<128)return false;
    *fs=(struct nova_fat16){0};
    fs->read=reader;fs->context=context;fs->device_sectors=device_sectors;
    uint8_t sector[512];
    if(!nova_fat_disk_read(fs,0,sector))return false;
    /* A sector with an explicit 512-byte BPB is handled as a superfloppy. */
    if(nova_fat_u16(sector+11)==512 && nova_fat_mount_at(fs,0,device_sectors))
        return true;
    /* Four primary MBR slots; never follow extended partition chains. */
    if(sector[510]!=0x55||sector[511]!=0xAA)return false;
    for(unsigned i=0;i<4;i++){
        const uint8_t *entry=sector+446+i*16u;
        uint8_t type=entry[4];
        if(type!=0x04&&type!=0x06&&type!=0x0E)continue;
        {
            uint32_t start=nova_fat_u32(entry+8);
            uint32_t sectors=nova_fat_u32(entry+12);
            if(!start||!sectors||start>=device_sectors||
               (uint64_t)start+sectors>device_sectors)continue;
            if(nova_fat_mount_at(fs,start,sectors))return true;
        }
    }
    return false;
}
static bool nova_fat16_entry(const struct nova_fat16 *fs,size_t position,
                             struct nova_fat_file *out){
    if(!fs||!fs->mounted||!out)return false;
    size_t found=0;
    uint8_t sec[512];
    for(uint32_t s=0;s<fs->root_sectors;s++){
        if(!nova_fat_disk_read(fs,fs->root_sector+s,sec))return false;
        for(unsigned i=0;i<16;i++){
            const uint8_t *p=sec+32u*i;
            if(p[0]==0)return false; /* End of directory */
            if(p[0]==0xE5||p[0]==0x05||(p[11]&0x08)||
               (p[11]&0x10)||p[11]==0x0F)continue;
            if(found++!=position)continue;
            size_t at=0;
            for(unsigned k=0;k<8&&p[k]!=' ';k++)
                if(at+1<sizeof(out->name))out->name[at++]=(char)p[k];
            if(p[8]!=' '){
                if(at+1<sizeof(out->name))out->name[at++]='.';
                for(unsigned k=8;k<11&&p[k]!=' ';k++)
                    if(at+1<sizeof(out->name))out->name[at++]=(char)p[k];
            }
            out->name[at]=0;
            out->first_cluster=nova_fat_u16(p+26);
            out->size=nova_fat_u32(p+28);
            return true;
        }
    }
    return false;
}
static bool nova_fat16_find(const struct nova_fat16 *fs,const char *name,
                            struct nova_fat_file *out){
    if(!name||!out)return false;
    for(size_t i=0;i<fs->root_entries;i++){
        struct nova_fat_file file;
        if(!nova_fat16_entry(fs,i,&file))return false;
        const char *a=file.name,*b=name;
        while(*a&&*b){if(*a++!=*b++)break;}
        if(!*a&&!*b){*out=file;return true;}
    }
    return false;
}
static bool nova_fat16_next(const struct nova_fat16 *fs,uint16_t cluster,
                            uint16_t *next){
    if(cluster<2||(uint32_t)cluster>=fs->cluster_count+2u)return false;
    uint32_t fat_offset=(uint32_t)cluster*2u;
    uint32_t sector=fat_offset/512u;
    if(sector>=fs->fat_sectors)return false;
    uint8_t buf[512];
    if(!nova_fat_disk_read(fs,fs->fat_sector+sector,buf))return false;
    *next=nova_fat_u16(buf+fat_offset%512u);
    return true;
}
/* Read a bounded range of a root-directory file. No write path exists.
 * Returns false on premature/invalid cluster chains or out-of-range reads. */
static bool nova_fat16_read(const struct nova_fat16 *fs,
                            const struct nova_fat_file *file,
                            uint32_t offset,uint8_t *out,size_t length){
    if(!fs||!fs->mounted||!file||(!out&&length))return false;
    if(offset>file->size||(uint64_t)offset+length>file->size)return false;
    if(!length)return true;
    if(file->first_cluster<2)return false;
    uint32_t cluster_bytes=(uint32_t)fs->sectors_per_cluster*512u;
    uint32_t skip=offset/cluster_bytes;
    if(skip>=fs->cluster_count)return false;
    uint16_t cluster=file->first_cluster;
    for(uint32_t i=0;i<skip;i++){
        uint16_t next;
        if(!nova_fat16_next(fs,cluster,&next)||next<2||
           (uint32_t)next>=fs->cluster_count+2u)return false;
        cluster=next;
    }
    size_t written=0;
    uint32_t inner=offset%cluster_bytes;
    uint32_t traversed=0;
    while(written<length){
        if(cluster<2||(uint32_t)cluster>=fs->cluster_count+2u||
           ++traversed>fs->cluster_count)return false;
        uint32_t sector_index=inner/512u;
        for(;sector_index<fs->sectors_per_cluster&&written<length;sector_index++){
            uint64_t lba=(uint64_t)fs->data_sector+
                        ((uint64_t)cluster-2u)*fs->sectors_per_cluster+sector_index;
            if(lba >= (uint64_t)fs->first_sector+fs->volume_sectors)return false;
            uint8_t sector[512];
            if(!nova_fat_disk_read(fs,(uint32_t)lba,sector))return false;
            uint32_t pos=inner%512u;
            while(pos<512u&&written<length)out[written++]=sector[pos++];
            inner=0;
        }
        if(written<length){
            uint16_t next;
            if(!nova_fat16_next(fs,cluster,&next)||next<2||
               (uint32_t)next>=fs->cluster_count+2u)return false;
            cluster=next;
        }
    }
    return true;
}
#endif
