/* NOVA VFS 0.10: unified access to two explicitly named volumes.
 *
 *    RAM:/NAME.TXT  - 12-file volatile writable RAM storage
 *    DISK:/NAME.TXT - FAT16 root directory, STRICTLY READ-ONLY
 *
 * This is not a POSIX VFS. No directories, symlinks, file descriptors,
 * user permissions, asynchronous operations, or disk-writing API.
 * No dynamic allocations. All paths and buffer lengths are bounded.
 */
#ifndef NOVA_VFS_H
#define NOVA_VFS_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "ramfs.h"
#include "fat16.h"

enum nova_vfs_volume { NOVA_VFS_NONE=0, NOVA_VFS_RAM, NOVA_VFS_DISK };
struct nova_vfs {
    const struct nova_fat16 *disk;
};
struct nova_vfs_entry {
    char name[NOVA_FILE_NAME_CAP];
    uint32_t size;
    bool read_only;
};
static bool nova_vfs_prefix(const char *path,enum nova_vfs_volume *volume,
                            const char **leaf){
    if(!path||!volume||!leaf)return false;
    if(path[0]=='R'&&path[1]=='A'&&path[2]=='M'&&path[3]==':'&&path[4]=='/'){
        *volume=NOVA_VFS_RAM;*leaf=path+5;
    }else if(path[0]=='D'&&path[1]=='I'&&path[2]=='S'&&path[3]=='K'&&
             path[4]==':'&&path[5]=='/'){
        *volume=NOVA_VFS_DISK;*leaf=path+6;
    }else return false;
    return true;
}
static bool nova_vfs_name(const char *name){
    if(!name||!name[0])return false;
    size_t i=0;
    for(;name[i];i++){
        if(i+1>=NOVA_FILE_NAME_CAP)return false;
        char c=name[i];
        if(!((c>='A'&&c<='Z')||(c>='0'&&c<='9')||
              c=='.'||c=='_'||c=='-'))return false;
    }
    return true;
}
static bool nova_vfs_list(const struct nova_vfs *vfs,const char *path,
                          size_t index,struct nova_vfs_entry *out){
    if(!vfs||!out)return false;
    enum nova_vfs_volume vol;const char *leaf;
    if(!nova_vfs_prefix(path,&vol,&leaf)||leaf[0])return false;
    *out=(struct nova_vfs_entry){0};
    if(vol==NOVA_VFS_RAM){
        int slot=nova_file_slot_by_ordinal(index);
        if(slot<0)return false;
        struct nova_file *file=&nova_files[slot];
        size_t i=0;
        while(file->name[i]&&i+1<sizeof(out->name)){
            out->name[i]=file->name[i];i++;
        }
        out->name[i]=0;out->size=(uint32_t)file->size;
        out->read_only=file->protected;
        return true;
    }
    if(vol==NOVA_VFS_DISK&&vfs->disk&&vfs->disk->mounted){
        struct nova_fat_file file;
        if(!nova_fat16_entry(vfs->disk,index,&file))return false;
        size_t i=0;
        while(file.name[i]&&i+1<sizeof(out->name)){
            out->name[i]=file.name[i];i++;
        }
        out->name[i]=0;out->size=file.size;out->read_only=true;
        return true;
    }
    return false;
}
static bool nova_vfs_read(const struct nova_vfs *vfs,const char *path,
                          size_t offset,uint8_t *out,size_t length){
    if(!vfs||(!out&&length))return false;
    enum nova_vfs_volume vol;const char *leaf;
    if(!nova_vfs_prefix(path,&vol,&leaf)||!nova_vfs_name(leaf))return false;
    if(vol==NOVA_VFS_RAM){
        struct nova_file *f=nova_file_find(leaf);
        if(!f||offset>f->size||length>f->size-offset)return false;
        for(size_t i=0;i<length;i++)out[i]=(uint8_t)f->data[offset+i];
        return true;
    }
    if(vol==NOVA_VFS_DISK&&vfs->disk&&vfs->disk->mounted){
        struct nova_fat_file file;
        if(!nova_fat16_find(vfs->disk,leaf,&file))return false;
        if(offset>UINT32_MAX)return false;
        return nova_fat16_read(vfs->disk,&file,(uint32_t)offset,out,length);
    }
    return false;
}
static bool nova_vfs_size(const struct nova_vfs *vfs,const char *path,
                          uint32_t *size,bool *read_only){
    if(!vfs||!size||!read_only)return false;
    enum nova_vfs_volume vol;const char *leaf;
    if(!nova_vfs_prefix(path,&vol,&leaf)||!nova_vfs_name(leaf))return false;
    if(vol==NOVA_VFS_RAM){
        struct nova_file *f=nova_file_find(leaf);
        if(!f)return false;
        *size=(uint32_t)f->size;*read_only=f->protected;
        return true;
    }
    if(vol==NOVA_VFS_DISK&&vfs->disk&&vfs->disk->mounted){
        struct nova_fat_file f;
        if(!nova_fat16_find(vfs->disk,leaf,&f))return false;
        *size=f.size;*read_only=true;
        return true;
    }
    return false;
}
static bool nova_vfs_write(const struct nova_vfs *vfs,const char *path,
                           const uint8_t *data,size_t length){
    if(!vfs||(!data&&length))return false;
    enum nova_vfs_volume vol;const char *leaf;
    if(!nova_vfs_prefix(path,&vol,&leaf)||vol!=NOVA_VFS_RAM||
       !nova_vfs_name(leaf))return false;
    return nova_file_write(leaf,(const char *)data,length);
}
static bool nova_vfs_create(const struct nova_vfs *vfs,const char *path){
    if(!vfs)return false;
    enum nova_vfs_volume vol;const char *leaf;
    if(!nova_vfs_prefix(path,&vol,&leaf)||vol!=NOVA_VFS_RAM||
       !nova_vfs_name(leaf))return false;
    return nova_file_create(leaf);
}
static bool nova_vfs_remove(const struct nova_vfs *vfs,const char *path){
    if(!vfs)return false;
    enum nova_vfs_volume vol;const char *leaf;
    if(!nova_vfs_prefix(path,&vol,&leaf)||vol!=NOVA_VFS_RAM||
       !nova_vfs_name(leaf))return false;
    return nova_file_delete(leaf);
}
#endif
