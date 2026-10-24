/* Small bounded in-memory virtual files: no disk writes or persistence.
 * All buffers are static kernel memory, with size checks at every write.
 */
#ifndef NOVA_RAMFS_H
#define NOVA_RAMFS_H
#include <stddef.h>
#include <stdbool.h>
struct nova_file {
    const char *name;
    char data[128];
    size_t size;
};
static struct nova_file nova_files[]={
    {"NOTES.TXT", "HELLO FROM NOVA!", 16},
    {"README.TXT", "NOVA Native uses its own kernel. This RAM disk does not survive reboot.", 69},
    {"TODO.TXT", "Mouse, storage, interrupts, drivers, processes, network and security.", 66}
};
#define NOVA_FILE_COUNT (sizeof(nova_files)/sizeof(nova_files[0]))
static bool nova_file_equal(const char *a,const char *b){
    while(*a&&*b){if(*a++!=*b++)return false;}
    return *a==*b;
}
static struct nova_file *nova_file_find(const char *name){
    for(size_t i=0;i<NOVA_FILE_COUNT;i++)
        if(nova_file_equal(name,nova_files[i].name))return &nova_files[i];
    return NULL;
}
static bool nova_file_write(const char *name,const char *data,size_t length){
    struct nova_file *file=nova_file_find(name);
    if(!file || length>=sizeof(file->data))return false;
    for(size_t i=0;i<length;i++)file->data[i]=data[i];
    file->data[length]=0;
    file->size=length;
    return true;
}
#endif
