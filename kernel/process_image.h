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
/* Trusted allocator callback: translates an allocated frame to its physical
 * page address. Pointer disjointness alone cannot detect HHDM aliases. */
typedef bool (*nova_process_phys_fn)(void *ctx,const void *page,uint64_t *physical);
/* Guarded callbacks carry the ORIGINAL allocation generation from the PMM.
 * A caller must not reconstruct that generation from a stale pointer.
 * All callbacks are synchronous, kernel-trusted, and single-core only. */
struct nova_process_guard_ops {
    bool (*allocate)(void *ctx,void **frame,uint64_t *generation);
    bool (*physical)(void *ctx,const void *frame,uint64_t generation,
                     uint64_t *physical);
    bool (*release)(void *ctx,void *frame,uint64_t generation);
    /* Owner-aware callbacks require the trusted owner's original ID,
     * never an owner reconstructed from an arbitrary/stale pointer. */
    bool (*allocate_owned)(void *ctx,uint64_t owner,void **frame,
                           uint64_t *generation);
    bool (*physical_owned)(void *ctx,uint64_t owner,const void *frame,
                           uint64_t generation,uint64_t *physical);
    bool (*release_owned)(void *ctx,uint64_t owner,void *frame,
                          uint64_t generation);
};
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
    uint64_t physical_page; /* validated physical identity at allocation */
    uint64_t generation; /* 0 for legacy, otherwise ORIGINAL PMM ticket */
    uint32_t flags;
};
struct nova_process_image {
    bool staged;
    bool guarded; /* requires original generation on every image frame */
    uint32_t pid;
    uint64_t owner; /* registered PMM owner ID, 0 for legacy; not a PID */
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
/* Pointer overlap is only a preliminary check. A trusted PMM-backed
 * physical-address callback is REQUIRED for staging and release. Distinct
 * virtual addresses may refer to the same physical frame. */
static inline bool nova_process_frame_overlaps(const void *a,const void *b){
    if(!a||!b)return false;
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x>=y ? x-y<4096u : y-x<4096u;
}
static inline bool nova_process_valid_physical(uint64_t phys){
    /* Match the address field of an x86-64 4-KiB PTE. */
    return phys!=0 && (phys&UINT64_C(0xFFF))==0 &&
           (phys&~UINT64_C(0x000FFFFFFFFFF000))==0;
}
/* Check physical identity and, for guarded frames, the allocation's
 * ORIGINAL generation. A pointer-only physical callback cannot validate a
 * guarded frame; it must never be used to release guarded image memory. */
static inline bool nova_process_page_identity_valid_owned(
    const struct nova_process_page *page,nova_process_phys_fn physical,
    void *context,const struct nova_process_guard_ops *guard,uint64_t owner){
    uint64_t observed=0;
    if(!page||!page->frame||!physical||
       ((uintptr_t)page->frame&4095u)!=0 ||
       !nova_process_valid_physical(page->physical_page))return false;
    if(page->generation){
        if(!guard)return false;
        if(owner){
            if(!guard->physical_owned ||
               !guard->physical_owned(context,owner,page->frame,
                                      page->generation,&observed))return false;
        }else if(!guard->physical ||
                 !guard->physical(context,page->frame,page->generation,
                                  &observed))return false;
    }else if(owner || !physical(context,page->frame,&observed))return false;
    return observed==page->physical_page;
}
static inline bool nova_process_page_identity_valid_guarded(
    const struct nova_process_page *page,nova_process_phys_fn physical,
    void *context,const struct nova_process_guard_ops *guard){
    return nova_process_page_identity_valid_owned(
        page,physical,context,guard,0);
}
static inline bool nova_process_page_identity_valid(
    const struct nova_process_page *page,nova_process_phys_fn physical,
    void *context){
    return nova_process_page_identity_valid_guarded(page,physical,context,NULL);
}
/* Validate the COMPLETE release set before any free. Unknown ownership is
 * deliberately leaked rather than potentially releasing someone else's
 * frame. Foreign guarded images also need a valid original generation. */
static inline bool nova_process_frames_releasable_guarded(
    const struct nova_process_image *image,
    const struct nova_process_table *table,
    nova_process_phys_fn physical,void *context,
    const struct nova_process_guard_ops *guard){
    if(!image||!physical||image->page_count>NOVA_PROCESS_MAX_PAGES||
       (image->guarded &&
        (!guard || (image->owner ?
          (!guard->physical_owned||!guard->release_owned) :
          (!guard->physical||!guard->release)))) ||
       (image->owner && !image->guarded))
        return false;
    for(uint32_t i=0;i<image->page_count;i++){
        const struct nova_process_page *page=&image->pages[i];
        if((image->guarded && !page->generation) ||
           (!image->guarded && page->generation) ||
           !nova_process_page_identity_valid_owned(
               page,physical,context,guard,image->owner))
            return false;
        for(uint32_t j=0;j<i;j++)
            if(nova_process_frame_overlaps(page->frame,image->pages[j].frame)||
               page->physical_page==image->pages[j].physical_page)
                return false;
        if(table){
            for(uint32_t slot=0;slot<NOVA_PROCESS_SLOTS;slot++){
                const struct nova_process_image *other=&table->slots[slot];
                if(other==image||!other->staged)continue;
                if(other->page_count>NOVA_PROCESS_MAX_PAGES)return false;
                for(uint32_t j=0;j<other->page_count;j++){
                    const struct nova_process_page *foreign=&other->pages[j];
                    if((other->guarded && !foreign->generation) ||
                       (!other->guarded && foreign->generation) ||
                       !nova_process_page_identity_valid_owned(
                           foreign,physical,context,guard,other->owner) ||
                       nova_process_frame_overlaps(page->frame,foreign->frame)||
                       page->physical_page==foreign->physical_page)
                        return false;
                }
            }
        }
    }
    return true;
}
static inline bool nova_process_frames_releasable(
    const struct nova_process_image *image,
    const struct nova_process_table *table,
    nova_process_phys_fn physical,void *context){
    return nova_process_frames_releasable_guarded(
        image,table,physical,context,NULL);
}
static inline bool nova_process_release_image_impl(
    struct nova_process_image *image,const struct nova_process_table *table,
    nova_process_free_fn free_page,nova_process_phys_fn physical,void *ctx,
    const struct nova_process_guard_ops *guard){
    if(!image)return false;
    const bool safe=(image->guarded ?
        (guard && (image->owner ? guard->release_owned!=NULL :
                                  guard->release!=NULL)) :
        (image->owner==0 && free_page!=NULL)) &&
        nova_process_frames_releasable_guarded(image,table,physical,ctx,guard);
    if(!safe)return false;
    /* Tail-first cleanup retains the exact remaining suffix after a callback
     * failure, so a later PDROP can retry without losing ownership metadata. */
    while(image->page_count){
        uint16_t i=(uint16_t)(image->page_count-1u);
        struct nova_process_page *p=&image->pages[i];
        bool released=true;
        if(image->guarded){
            if(image->owner)
                released=guard->release_owned(ctx,image->owner,p->frame,
                                               p->generation);
            else
                released=guard->release(ctx,p->frame,p->generation);
        }else free_page(ctx,p->frame);
        if(!released)return false;
        *p=(struct nova_process_page){0};
        image->page_count=i;
    }
    *image=(struct nova_process_image){0};
    return true;
}
static inline bool nova_process_release_image(
    struct nova_process_image *image,const struct nova_process_table *table,
    nova_process_free_fn free_page,nova_process_phys_fn physical,void *ctx){
    /* A legacy release must not erase the only saved generation tickets. */
    if(image && image->guarded)return false;
    return nova_process_release_image_impl(
        image,table,free_page,physical,ctx,NULL);
}
static inline bool nova_process_drop(
    struct nova_process_table *table,uint32_t pid,
    nova_process_free_fn free_page,nova_process_phys_fn physical,void *ctx){
    if(!free_page||!physical)return false;
    struct nova_process_image *image=nova_process_find(table,pid);
    if(!image||image->guarded)return false;
    return nova_process_release_image_impl(
        image,table,free_page,physical,ctx,NULL);
}
static inline bool nova_process_drop_guarded(
    struct nova_process_table *table,uint32_t pid,
    nova_process_phys_fn physical,const struct nova_process_guard_ops *guard,
    void *ctx){
    if(!physical||!guard)return false;
    struct nova_process_image *image=nova_process_find(table,pid);
    if(!image||!image->guarded ||
       (image->owner ? (!guard->physical_owned||!guard->release_owned) :
                       (!guard->physical||!guard->release)))return false;
    return nova_process_release_image_impl(
        image,table,NULL,physical,ctx,guard);
}
/* Before zeroing/copying a candidate frame, reject pointer and physical
 * aliases against the current image and all staged foreign images. */
static inline bool nova_process_frame_available_owned(
    const struct nova_process_table *table,
    const struct nova_process_image *current,const void *frame,
    uint64_t generation,nova_process_phys_fn physical,void *context,
    const struct nova_process_guard_ops *guard,uint64_t *physical_out){
    if(!frame||!table||!current||!physical||!physical_out||
       ((uintptr_t)frame&4095u)!=0||
       current->page_count>NOVA_PROCESS_MAX_PAGES ||
       (current->guarded &&
        (!generation||!guard||
         (current->owner ? !guard->physical_owned : !guard->physical))) ||
       (!current->guarded && (generation||current->owner)))return false;
    uint64_t candidate=0;
    if(current->guarded){
        if(current->owner){
            if(!guard->physical_owned(context,current->owner,frame,generation,
                                      &candidate))return false;
        }else if(!guard->physical(context,frame,generation,&candidate))
            return false;
    }else if(!physical(context,frame,&candidate))return false;
    if(!nova_process_valid_physical(candidate))return false;
    for(uint32_t j=0;j<current->page_count;j++){
        const struct nova_process_page *page=&current->pages[j];
        if((current->guarded && !page->generation) ||
           (!current->guarded && page->generation) ||
           !nova_process_page_identity_valid_owned(
               page,physical,context,guard,current->owner) ||
           nova_process_frame_overlaps(frame,page->frame)||
           candidate==page->physical_page)return false;
    }
    for(uint32_t slot=0;slot<NOVA_PROCESS_SLOTS;slot++){
        const struct nova_process_image *other=&table->slots[slot];
        if(other==current||!other->staged)continue;
        if(other->page_count>NOVA_PROCESS_MAX_PAGES)return false;
        for(uint32_t j=0;j<other->page_count;j++){
            const struct nova_process_page *page=&other->pages[j];
            if((other->guarded && !page->generation) ||
               (!other->guarded && page->generation) ||
               !nova_process_page_identity_valid_owned(
                   page,physical,context,guard,other->owner) ||
               nova_process_frame_overlaps(frame,page->frame)||
               candidate==page->physical_page)return false;
        }
    }
    *physical_out=candidate;
    return true;
}
static inline bool nova_process_frame_available_guarded(
    const struct nova_process_table *table,
    const struct nova_process_image *current,const void *frame,
    uint64_t generation,nova_process_phys_fn physical,void *context,
    const struct nova_process_guard_ops *guard,uint64_t *physical_out){
    return nova_process_frame_available_owned(
        table,current,frame,generation,physical,context,guard,physical_out);
}
static inline bool nova_process_frame_available(
    const struct nova_process_table *table,
    const struct nova_process_image *current,const void *frame,
    nova_process_phys_fn physical,void *context,uint64_t *physical_out){
    return nova_process_frame_available_guarded(
        table,current,frame,0,physical,context,NULL,physical_out);
}
/* Stages checked ELF bytes but never maps them into active CR3 or executes
 * any user instructions. The guarded variant uses allocation-time tickets
 * for every ELF page and on all rollback/drop paths. */
static inline enum nova_process_result nova_process_stage_impl(
        struct nova_process_table *table,const char *label,
        nova_elf_read_fn read_file,void *read_context,uint64_t file_size,
        nova_process_alloc_fn allocate,nova_process_free_fn free_page,
        nova_process_phys_fn physical,const struct nova_process_guard_ops *guard,
        uint64_t owner,void *page_context,uint32_t *pid_out){
    if(pid_out)*pid_out=0;
    if(!table||!label||!read_file||!physical||!pid_out ||
       (guard ? (owner ?
           (!guard->allocate_owned||!guard->physical_owned||
            !guard->release_owned) :
           (!guard->allocate||!guard->physical||!guard->release))
              : (owner||!allocate||!free_page)))
        return NOVA_PROCESS_BAD_INPUT;
    struct nova_elf_info elf;
    if(nova_elf64_probe(read_file,read_context,file_size,&elf)!=NOVA_ELF_OK)
        return NOVA_PROCESS_INVALID_ELF;
    if(elf.pages>NOVA_PROCESS_MAX_PAGES)return NOVA_PROCESS_TOO_LARGE;
    struct nova_process_image *image=NULL;
    for(uint32_t i=0;i<NOVA_PROCESS_SLOTS;i++){
        if(!table->slots[i].staged){image=&table->slots[i];break;}
    }
    if(!image || table->next_pid==UINT32_MAX)return NOVA_PROCESS_NO_SLOT;
    *image=(struct nova_process_image){0};
    image->guarded=guard!=NULL;
    image->owner=owner;
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
                (void)nova_process_release_image_impl(
                    image,table,free_page,physical,page_context,guard);
                return NOVA_PROCESS_TOO_LARGE;
            }
            void *raw=NULL;
            uint64_t generation=0;
            if(guard){
                if(owner){
                    if(!guard->allocate_owned(page_context,owner,&raw,
                                              &generation))raw=NULL;
                }else if(!guard->allocate(page_context,&raw,&generation))
                    raw=NULL;
            }else raw=allocate(page_context);
            uint8_t *frame=(uint8_t*)raw;
            if(!frame){
                (void)nova_process_release_image_impl(
                    image,table,free_page,physical,page_context,guard);
                return NOVA_PROCESS_NO_MEMORY;
            }
            uint64_t frame_phys=0;
            if(!nova_process_frame_available_guarded(
                    table,image,frame,generation,physical,page_context,
                    guard,&frame_phys)){
                /* Suspect allocation: do not zero OR free it. */
                (void)nova_process_release_image_impl(
                    image,table,free_page,physical,page_context,guard);
                return NOVA_PROCESS_BAD_FRAME;
            }
            /* Do not trust the allocator to clear previously used memory. */
            for(uint32_t b=0;b<4096;b++)frame[b]=0;
            struct nova_process_page *p=&image->pages[image->page_count++];
            p->frame=frame;p->physical_page=frame_phys;
            p->generation=generation;
            p->virtual_page=first+(uint64_t)page*4096u;
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
                (void)nova_process_release_image_impl(
                    image,table,free_page,physical,page_context,guard);
                return NOVA_PROCESS_READ_ERROR;
            }
            copied+=length;
        }
    }
    image->staged=true;
    image->pid=++table->next_pid;
    *pid_out=image->pid;
    return NOVA_PROCESS_OK;
}
static inline enum nova_process_result nova_process_stage(
        struct nova_process_table *table,const char *label,
        nova_elf_read_fn read_file,void *read_context,uint64_t file_size,
        nova_process_alloc_fn allocate,nova_process_free_fn free_page,
        nova_process_phys_fn physical,void *page_context,uint32_t *pid_out){
    return nova_process_stage_impl(table,label,read_file,read_context,file_size,
        allocate,free_page,physical,NULL,0,page_context,pid_out);
}
static inline enum nova_process_result nova_process_stage_guarded(
        struct nova_process_table *table,const char *label,
        nova_elf_read_fn read_file,void *read_context,uint64_t file_size,
        nova_process_phys_fn physical,const struct nova_process_guard_ops *guard,
        void *page_context,uint32_t *pid_out){
    return nova_process_stage_impl(table,label,read_file,read_context,file_size,
        NULL,NULL,physical,guard,0,page_context,pid_out);
}
/* Owner-bound staging is offline-only. The trusted caller registers the
 * owner first and retires it after successful release. Suspect frames are
 * deliberately leaked, and therefore block owner retirement. */
static inline enum nova_process_result nova_process_stage_owned_guarded(
        struct nova_process_table *table,const char *label,
        nova_elf_read_fn read_file,void *read_context,uint64_t file_size,
        nova_process_phys_fn physical,const struct nova_process_guard_ops *guard,
        uint64_t owner,void *page_context,uint32_t *pid_out){
    if(!owner){
        if(pid_out)*pid_out=0;
        return NOVA_PROCESS_BAD_INPUT;
    }
    return nova_process_stage_impl(table,label,read_file,read_context,file_size,
        NULL,NULL,physical,guard,owner,page_context,pid_out);
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
