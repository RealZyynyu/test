/* NOVA: bounded STAGED process images, NOT runnable processes.
 *
 * Builds separate in-memory images from checked static x86-64 ELF binaries.
 * Allocates up to 32 physical pages per image, copies PT_LOAD file bytes,
 * zero-fills BSS, tracks intended virtual addresses and W^X metadata.
 *
 * SECURITY / IMPLEMENTATION LIMIT:
 * - No per-process page tables; no CR3 changes or ring-3 entry.
 * - Virtual addresses are just metadata; frames are still kernel-addressable.
 * - Flags describe intended protections, NOT hardware-enforced rights.
 * - No scheduling, system calls, privilege isolation, execution or threads.
 * - Each image is limited to 128 KiB and can be freed atomically.
 *
 * The allocator and read callbacks allow host tests without hardware.
 */
#ifndef NOVA_PROCESS_IMAGE_H
#define NOVA_PROCESS_IMAGE_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "elf64.h"

#define NOVA_PROCESS_SLOTS 4u
#define NOVA_PROCESS_MAX_PAGES 32u
#define NOVA_PROCESS_LABEL_CAP 40u

typedef void *(*nova_process_alloc_fn)(void *ctx);
typedef void (*nova_process_free_fn)(void *ctx,void *page);
enum nova_process_result {
    NOVA_PROCESS_OK=0,
    NOVA_PROCESS_BAD_INPUT,
    NOVA_PROCESS_INVALID_ELF,
    NOVA_PROCESS_TOO_LARGE,
    NOVA_PROCESS_NO_SLOT,
    NOVA_PROCESS_NO_MEMORY,
    NOVA_PROCESS_READ_ERROR,
    NOVA_PROCESS_BAD_FRAME
};
struct nova_process_page {
    uint64_t virtual_page;
    uint8_t *frame; /* physical memory viewed through HHDM; NOT ring-3 mapping */
    uint32_t flags;
};
struct nova_process_image {
    bool staged;
    uint32_t pid;
    uint64_t entry;
    uint16_t segments;
    uint16_t page_count;
    char label[NOVA_PROCESS_LABEL_CAP];
    struct nova_process_page pages[NOVA_PROCESS_MAX_PAGES];
};
struct nova_process_table {
    uint32_t next_pid;
    struct nova_process_image slots[NOVA_PROCESS_SLOTS];
};

static inline void nova_process_copy_label(char *dest,const char *source){
    size_t k=0;
    if(source){
        for(;source[k] && k+1<NOVA_PROCESS_LABEL_CAP;k++){
            char c=source[k];
            dest[k]=(c>=32&&c<=126)?c:'?';
        }
    }
    dest[k]=0;
}
static inline const char *nova_process_result_name(enum nova_process_result result){
    switch(result){
        case NOVA_PROCESS_OK: return "IMAGE STAGED - NOT EXECUTABLE YET";
        case NOVA_PROCESS_BAD_INPUT: return "INVALID PROCESS REQUEST";
        case NOVA_PROCESS_INVALID_ELF: return "ELF VALIDATION REJECTED";
        case NOVA_PROCESS_TOO_LARGE: return "IMAGE EXCEEDS 32 PAGE LIMIT";
        case NOVA_PROCESS_NO_SLOT: return "PROCESS IMAGE TABLE FULL";
        case NOVA_PROCESS_NO_MEMORY: return "INSUFFICIENT PAGE FRAMES";
        case NOVA_PROCESS_READ_ERROR: return "PROGRAM IMAGE READ ERROR";
        case NOVA_PROCESS_BAD_FRAME: return "ALLOCATOR RETURNED AN ALIASED PAGE";
        default: return "UNKNOWN PROCESS ERROR";
    }
}
static inline uint32_t nova_process_count(const struct nova_process_table *table){
    if(!table)return 0;
    uint32_t n=0;
    for(uint32_t i=0;i<NOVA_PROCESS_SLOTS;i++)
        if(table->slots[i].staged)n++;
    return n;
}
static inline struct nova_process_image *nova_process_find(
        struct nova_process_table *table,uint32_t pid){
    if(!table||!pid)return NULL;
    for(uint32_t i=0;i<NOVA_PROCESS_SLOTS;i++)
        if(table->slots[i].staged && table->slots[i].pid==pid)
            return &table->slots[i];
    return NULL;
}
/* A newly returned 4-KiB frame must not overlap any already owned
 * frame, even if a buggy allocator returns an interior pointer. Compare
 * address differences rather than adding 4096 (which could wrap).
 * This proves only virtual-pointer disjointness, not physical identity. */
static inline bool nova_process_frame_overlaps(const void *a,const void *b){
    if(!a||!b)return false;
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x>=y ? x-y<4096u : y-x<4096u;
}
/* Check an entire release set BEFORE freeing the first page. A corrupt
 * count or duplicate frame must not produce OOB access or double-free.
 * The optional process table also protects frames of other staged images.
 * Unverifiable frames are deliberately leaked, not released. */
static inline bool nova_process_frames_releasable(
    const struct nova_process_image *image,
    const struct nova_process_table *table){
    if(!image||image->page_count>NOVA_PROCESS_MAX_PAGES)return false;
    for(uint32_t i=0;i<image->page_count;i++){
        const void *frame=image->pages[i].frame;
        if(!frame||((uintptr_t)frame&4095u)!=0)return false;
        for(uint32_t j=0;j<i;j++)
            if(nova_process_frame_overlaps(frame,image->pages[j].frame))
                return false;
        if(table){
            for(uint32_t slot=0;slot<NOVA_PROCESS_SLOTS;slot++){
                const struct nova_process_image *other=&table->slots[slot];
                if(other==image||!other->staged)continue;
                if(other->page_count>NOVA_PROCESS_MAX_PAGES)return false;
                for(uint32_t j=0;j<other->page_count;j++)
                    if(!other->pages[j].frame ||
                       nova_process_frame_overlaps(frame,other->pages[j].frame))
                        return false;
            }
        }
    }
    return true;
}
/* The return value reports whether any owned pages were safely released.
 * On corrupted metadata discard the image descriptor without freeing
 * suspicious frames; callers must not treat this as a successful drop. */
static inline bool nova_process_release_image(struct nova_process_image *image,
                        nova_process_free_fn free_page,void *ctx){
    if(!image)return false;
    bool safe=free_page && nova_process_frames_releasable(image,NULL);
    if(safe){
        for(uint32_t i=0;i<image->page_count;i++)
            free_page(ctx,image->pages[i].frame);
    }
    *image=(struct nova_process_image){0};
    return safe;
}
static inline bool nova_process_drop(struct nova_process_table *table,
             uint32_t pid,nova_process_free_fn free_page,void *ctx){
    if(!free_page)return false;
    struct nova_process_image *image=nova_process_find(table,pid);
    if(!image)return false;
    if(!nova_process_frames_releasable(image,table)){
        *image=(struct nova_process_image){0};
        return false;
    }
    return nova_process_release_image(image,free_page,ctx);
}
/* Reject borrowed/overlapping allocator results BEFORE clearing any bytes.
 * Include other staged processes, not just the image being constructed. */
static inline bool nova_process_frame_available(
    const struct nova_process_table *table,
    const struct nova_process_image *current,const void *frame){
    if(!frame||!table||!current||((uintptr_t)frame&4095u)!=0)
        return false;
    if(current->page_count>NOVA_PROCESS_MAX_PAGES)return false;
    for(uint32_t j=0;j<current->page_count;j++)
        if(!current->pages[j].frame ||
           nova_process_frame_overlaps(frame,current->pages[j].frame))
            return false;
    for(uint32_t slot=0;slot<NOVA_PROCESS_SLOTS;slot++){
        const struct nova_process_image *other=&table->slots[slot];
        if(other==current||!other->staged)continue;
        if(other->page_count>NOVA_PROCESS_MAX_PAGES)return false;
        for(uint32_t j=0;j<other->page_count;j++)
            if(!other->pages[j].frame ||
               nova_process_frame_overlaps(frame,other->pages[j].frame))
                return false;
    }
    return true;
}
/* Loads into privately allocated physical frames, but does NOT map
 * those frames into user-space page tables or execute any instructions.
 */
static inline enum nova_process_result nova_process_stage(
        struct nova_process_table *table,const char *label,
        nova_elf_read_fn read_file,void *read_context,uint64_t file_size,
        nova_process_alloc_fn allocate,nova_process_free_fn free_page,
        void *page_context,uint32_t *pid_out){
    if(pid_out)*pid_out=0;
    if(!table||!label||!read_file||!allocate||!free_page||!pid_out)
        return NOVA_PROCESS_BAD_INPUT;
    struct nova_elf_info elf;
    if(nova_elf64_probe(read_file,read_context,file_size,&elf)!=NOVA_ELF_OK)
        return NOVA_PROCESS_INVALID_ELF;
    if(elf.pages>NOVA_PROCESS_MAX_PAGES)return NOVA_PROCESS_TOO_LARGE;
    struct nova_process_image *image=NULL;
    for(uint32_t i=0;i<NOVA_PROCESS_SLOTS;i++){
        if(!table->slots[i].staged){
            image=&table->slots[i];break;
        }
    }
    if(!image || table->next_pid==UINT32_MAX)return NOVA_PROCESS_NO_SLOT;
    *image=(struct nova_process_image){0};
    image->entry=elf.entry;
    image->segments=elf.load_count;
    nova_process_copy_label(image->label,label);
    for(uint32_t si=0;si<elf.load_count;si++){
        const struct nova_elf_segment *s=&elf.loads[si];
        uint64_t first=s->virtual_address&~UINT64_C(4095);
        uint64_t end=s->virtual_address+s->memory_size;
        uint64_t aligned_end=(end+4095u)&~UINT64_C(4095);
        uint32_t pages=(uint32_t)((aligned_end-first)/4096u);
        uint32_t first_index=image->page_count;
        for(uint32_t page=0;page<pages;page++){
            if(image->page_count>=NOVA_PROCESS_MAX_PAGES){
                nova_process_release_image(image,free_page,page_context);
                return NOVA_PROCESS_TOO_LARGE;
            }
            uint8_t *frame=(uint8_t*)allocate(page_context);
            if(!frame){
                (void)nova_process_release_image(image,free_page,page_context);
                return NOVA_PROCESS_NO_MEMORY;
            }
            if(!nova_process_frame_available(table,image,frame)){
                /* Borrowed frames are not ours: do not zero or free them. */
                (void)nova_process_release_image(image,free_page,page_context);
                return NOVA_PROCESS_BAD_FRAME;
            }
            /* Do not trust the allocator to clear previously used memory. */
            for(uint32_t b=0;b<4096;b++)frame[b]=0;
            struct nova_process_page *p=&image->pages[image->page_count++];
            p->frame=frame;p->virtual_page=first+(uint64_t)page*4096u;
            p->flags=s->flags;
        }
        uint64_t copied=0;
        while(copied<s->file_size){
            uint64_t virtual_addr=s->virtual_address+copied;
            uint64_t relative=virtual_addr-first;
            uint32_t index=first_index+(uint32_t)(relative/4096u);
            uint32_t in_page=(uint32_t)(relative&4095u);
            size_t length=(size_t)(4096u-in_page);
            if((uint64_t)length>s->file_size-copied)
                length=(size_t)(s->file_size-copied);
            if(index>=image->page_count ||
               !nova_elf_read_exact(read_file,read_context,file_size,
                      s->file_offset+copied,image->pages[index].frame+in_page,
                      length)){
                nova_process_release_image(image,free_page,page_context);
                return NOVA_PROCESS_READ_ERROR;
            }
            copied+=length;
        }
        /* Bytes up to memory_size but past file_size remain zero (BSS). */
    }
    image->staged=true;
    image->pid=++table->next_pid;
    *pid_out=image->pid;
    return NOVA_PROCESS_OK;
}
/* Debugger-only read of staged bytes. Does not enforce user privileges. */
static inline bool nova_process_peek(const struct nova_process_image *image,
                 uint64_t vaddr,uint8_t *out,size_t size){
    if(!image||!image->staged||(!out&&size)||
       image->page_count>NOVA_PROCESS_MAX_PAGES)return false;
    for(size_t i=0;i<size;i++){
        if(vaddr>UINT64_MAX-i)return false;
        uint64_t addr=vaddr+i,base=addr&~UINT64_C(4095);
        bool found=false;
        for(uint32_t j=0;j<image->page_count;j++){
            if(image->pages[j].virtual_page==base){
                if(!image->pages[j].frame)return false;
                out[i]=image->pages[j].frame[addr&4095u];
                found=true;break;
            }
        }
        if(!found)return false;
    }
    return true;
}
#endif
