/* NOVA bounded in-memory virtual filesystem, version 0.8.
 * 12 static slots, 128 bytes per file. No persistence or disk I/O.
 * Names: 1-19 uppercase ASCII, digits, dot, underscore, dash.
 * No directories, permissions, users, or cross-process synchronization.
 */
#ifndef NOVA_RAMFS_H
#define NOVA_RAMFS_H
#include <stddef.h>
#include <stdbool.h>
#define NOVA_FILE_COUNT 12u
#define NOVA_FILE_DATA_CAP 128u
#define NOVA_FILE_NAME_CAP 20u
struct nova_file {
    char name[NOVA_FILE_NAME_CAP];
    char data[NOVA_FILE_DATA_CAP];
    size_t size;
    bool used, protected;
};
static struct nova_file nova_files[NOVA_FILE_COUNT]={
    {.name="NOTES.TXT",.data="HELLO FROM NOVA!",.size=16,.used=true,.protected=true},
    {.name="README.TXT",.data="NOVA OS is an independent kernel. RAM files vanish at reboot.",.size=61,.used=true,.protected=true},
    {.name="TODO.TXT",.data="Persistent storage, processes, security, USB, GPU and networking.",.size=63,.used=true,.protected=true}
};
static bool nova_file_equal(const char *a,const char *b){
    while(*a&&*b){if(*a++!=*b++)return false;}
    return *a==*b;
}
static bool nova_file_valid_name(const char *name){
    if(!name)return false;
    size_t length=0;
    for(;name[length];length++){
        char c=name[length];
        if(length+1>=NOVA_FILE_NAME_CAP)return false;
        if(!((c>='A'&&c<='Z')||(c>='0'&&c<='9')||
              c=='.'||c=='_'||c=='-'))return false;
    }
    return length>0;
}
static struct nova_file *nova_file_find(const char *name){
    if(!name)return NULL;
    for(size_t i=0;i<NOVA_FILE_COUNT;i++)
        if(nova_files[i].used&&nova_file_equal(name,nova_files[i].name))
            return &nova_files[i];
    return NULL;
}
static size_t nova_file_count(void){
    size_t count=0;
    for(size_t i=0;i<NOVA_FILE_COUNT;i++)
        if(nova_files[i].used)count++;
    return count;
}
static int nova_file_slot_by_ordinal(size_t ordinal){
    for(size_t i=0;i<NOVA_FILE_COUNT;i++){
        if(!nova_files[i].used)continue;
        if(ordinal==0)return (int)i;
        ordinal--;
    }
    return -1;
}
static bool nova_file_create(const char *name){
    if(!nova_file_valid_name(name)||nova_file_find(name))return false;
    for(size_t i=0;i<NOVA_FILE_COUNT;i++){
        struct nova_file *f=&nova_files[i];
        if(f->used)continue;
        size_t k=0;while(name[k]){f->name[k]=name[k];k++;}
        f->name[k]=0;f->data[0]=0;f->size=0;f->protected=false;f->used=true;
        return true;
    }
    return false;
}
static bool nova_file_write(const char *name,const char *data,size_t length){
    struct nova_file *f=nova_file_find(name);
    if(!f||!data||length>=NOVA_FILE_DATA_CAP)return false;
    for(size_t i=0;i<length;i++)f->data[i]=data[i];
    f->data[length]=0;f->size=length;
    return true;
}
static bool nova_file_delete(const char *name){
    struct nova_file *f=nova_file_find(name);
    if(!f||f->protected)return false;
    f->used=false;f->size=0;f->name[0]=0;f->data[0]=0;
    return true;
}
static bool nova_ramfs_self_test(void){
    const char *temp="TEST.NVA";
    if(nova_file_find(temp))return false;
    size_t before=nova_file_count();
    if(!nova_file_create(temp)||nova_file_count()!=before+1)return false;
    if(nova_file_create(temp))return false;
    if(!nova_file_write(temp,"PASS",4))return false;
    struct nova_file *f=nova_file_find(temp);
    if(!f||f->size!=4||!nova_file_equal(f->data,"PASS"))return false;
    if(nova_file_delete("NOTES.TXT"))return false;
    if(!nova_file_delete(temp)||nova_file_find(temp))return false;
    return nova_file_count()==before;
}
#endif
