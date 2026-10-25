/* NOVA physical page allocator (developer prototype).
 * Uses only Limine USABLE regions, maps through the Limine HHDM, and
 * manages a bounded 8 MiB pool. No virtual address spaces or isolation.
 * Never touches disk. Freestanding, single-core, no interrupt concurrency.
 * Rejects overflowed memory ranges and wipes frames on release.
 */
#ifndef NOVA_PMM_H
#define NOVA_PMM_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>
#define NOVA_PAGE_SIZE 4096ull
#define NOVA_PAGE_POOL_CAP 2048u
#define NOVA_BITMAP_SIZE ((NOVA_PAGE_POOL_CAP + 7u) / 8u)
#define NOVA_OWNER_SLOT_CAP 64u
/* Global allocation ROLE, not a process identity or cryptographic ownership
 * proof. Roles prevent an image frame from being freed as a page table/stack.
 * All operations remain single-core and non-reentrant. */
enum nova_frame_role {
    NOVA_FRAME_FREE=0,
    NOVA_FRAME_GENERAL=1,
    NOVA_FRAME_IMAGE=2,
    NOVA_FRAME_PAGE_TABLE=3,
    NOVA_FRAME_STACK=4
};
static bool nova_pmm_role_valid(enum nova_frame_role role){
    return role>=NOVA_FRAME_GENERAL && role<=NOVA_FRAME_STACK;
}
struct nova_page_manager {
    uintptr_t hhdm_offset;
    uint64_t phys_base;
    uint32_t pages, in_use;
    uint8_t bitmap[NOVA_BITMAP_SIZE];
    uint8_t roles[NOVA_PAGE_POOL_CAP]; /* one role per physical 4-KiB frame */
    /* Generation changes on EVERY allocation, including same-role reuse.
     * A ticket issued for an old allocation cannot authorize the next.
     * No wraparound: allocation fails closed at the counter limit. */
    uint64_t generations[NOVA_PAGE_POOL_CAP];
    /* 0 denotes legacy kernel allocations; nonzero IDs are supplied by a
     * trusted caller and bound to an allocation ticket, NOT to a PID yet. */
    uint64_t owners[NOVA_PAGE_POOL_CAP];
    uint64_t owner_registry[NOVA_OWNER_SLOT_CAP]; /* active trusted IDs */
    uint64_t next_owner; /* monotonic; retired IDs never reused */
    uint64_t next_generation;
    bool initialized;
};
static struct nova_page_manager nova_pmm;
static uint64_t nova_align_up(uint64_t value,uint64_t align){
    return (value+(align-1))&~(align-1);
}
static bool nova_pmm_owner_active(uint64_t owner){
    if(!owner)return false;
    for(uint32_t i=0;i<NOVA_OWNER_SLOT_CAP;i++)
        if(nova_pmm.owner_registry[i]==owner)return true;
    return false;
}
/* Reject damaged PMM bookkeeping BEFORE indexing the bitmap, wiping a
 * frame, or returning a physical address. This is a single-core consistency
 * check, not an authenticated ownership proof or a concurrency primitive.
 * The pool is deliberately small (at most 2048 frames); a full bitmap scan
 * is preferable to acting on inconsistent allocation metadata.
 */
static bool nova_pmm_metadata_valid(void){
    if(!nova_pmm.initialized || nova_pmm.next_generation==0 ||
       nova_pmm.next_owner==0 || nova_pmm.pages<8u ||
       nova_pmm.pages>NOVA_PAGE_POOL_CAP ||
       nova_pmm.in_use>nova_pmm.pages ||
       (nova_pmm.phys_base&(NOVA_PAGE_SIZE-1u))!=0 ||
       (nova_pmm.hhdm_offset&(NOVA_PAGE_SIZE-1u))!=0)return false;
    const uint64_t span=(uint64_t)nova_pmm.pages*NOVA_PAGE_SIZE;
    if(nova_pmm.phys_base>UINTPTR_MAX-(span-1u))return false;
    const uintptr_t last=(uintptr_t)(nova_pmm.phys_base+span-1u);
    if(nova_pmm.hhdm_offset>UINTPTR_MAX-last)return false;

    const uint32_t used_bytes=(nova_pmm.pages+7u)/8u;
    uint32_t counted=0;
    for(uint32_t i=0;i<NOVA_BITMAP_SIZE;i++){
        const uint8_t bits=nova_pmm.bitmap[i];
        /* Unused bitmap bytes and tail bits must remain clear. */
        if(i>=used_bytes){
            if(bits)return false;
            continue;
        }
        if(i==nova_pmm.pages/8u && (nova_pmm.pages&7u)!=0){
            const uint8_t allowed=(uint8_t)((1u<<(nova_pmm.pages&7u))-1u);
            if((bits&(uint8_t)~allowed)!=0)return false;
        }
        for(unsigned bit=0;bit<8;bit++)
            counted+=(uint32_t)((bits>>bit)&1u);
    }
    /* Reject duplicate, future or otherwise invalid active IDs. */
    for(uint32_t i=0;i<NOVA_OWNER_SLOT_CAP;i++){
        const uint64_t id=nova_pmm.owner_registry[i];
        if(!id)continue;
        if(id>=nova_pmm.next_owner)return false;
        for(uint32_t j=0;j<i;j++)
            if(nova_pmm.owner_registry[j]==id)return false;
    }
    /* Bitmap and role ledger must agree, including every unused slot.
     * A role mismatch fails closed before any page can be overwritten. */
    for(uint32_t i=0;i<NOVA_PAGE_POOL_CAP;i++){
        const bool occupied=i<nova_pmm.pages &&
            (nova_pmm.bitmap[i>>3]&(uint8_t)(1u<<(i&7u)))!=0;
        const uint8_t role=nova_pmm.roles[i];
        const uint64_t generation=nova_pmm.generations[i];
        const uint64_t owner=nova_pmm.owners[i];
        if(occupied){
            if(role<NOVA_FRAME_GENERAL || role>NOVA_FRAME_STACK ||
               generation==0 || generation>=nova_pmm.next_generation ||
               (owner!=0 && !nova_pmm_owner_active(owner)))
                return false;
        }else if(role!=NOVA_FRAME_FREE || generation!=0 || owner!=0)
            return false;
    }
    return counted==nova_pmm.in_use;
}
static bool nova_pmm_init(volatile struct limine_memmap_response *mm,
                          volatile struct limine_hhdm_response *hhdm){
    /* Reinitializing an active allocator would lose all page ownership. */
    if(nova_pmm.initialized || !mm || !hhdm || !mm->entries)return false;
    uint64_t base=0;
    uint32_t pages=0;
    const uint64_t offset=(uint64_t)hhdm->offset;
    /* A misaligned HHDM would produce unaligned page pointers. */
    if((offset&(NOVA_PAGE_SIZE-1u))!=0)return false;
    for(uint64_t i=0;i<mm->entry_count;i++){
        struct limine_memmap_entry *entry=mm->entries[i];
        if(!entry || entry->type!=LIMINE_MEMMAP_USABLE ||
           entry->length<NOVA_PAGE_SIZE)continue;
        /* Reject malicious/corrupt BIOS region bounds before arithmetic. */
        if(entry->base>UINT64_MAX-entry->length ||
           entry->base>UINT64_MAX-(NOVA_PAGE_SIZE-1u))continue;
        const uint64_t end=entry->base+entry->length;
        const uint64_t first=nova_align_up(entry->base,NOVA_PAGE_SIZE);
        if(first>=end)continue;
        const uint64_t available=(end-first)/NOVA_PAGE_SIZE;
        const uint32_t count=available>NOVA_PAGE_POOL_CAP?
                             NOVA_PAGE_POOL_CAP:(uint32_t)available;
        if(count<=pages)continue;
        /* Entire HHDM mapped pool must fit uintptr_t without wraparound. */
        const uint64_t last=first+(uint64_t)count*NOVA_PAGE_SIZE-1u;
        if(last>UINTPTR_MAX || offset>UINTPTR_MAX-last)continue;
        pages=count;
        base=first;
        if(pages==NOVA_PAGE_POOL_CAP)break;
    }
    if(pages<8)return false;
    for(size_t i=0;i<NOVA_BITMAP_SIZE;i++)nova_pmm.bitmap[i]=0;
    for(size_t i=0;i<NOVA_PAGE_POOL_CAP;i++){
        nova_pmm.roles[i]=NOVA_FRAME_FREE;
        nova_pmm.generations[i]=0;
        nova_pmm.owners[i]=0;
    }
    for(size_t i=0;i<NOVA_OWNER_SLOT_CAP;i++)nova_pmm.owner_registry[i]=0;
    nova_pmm.next_owner=1;
    nova_pmm.next_generation=1;
    nova_pmm.phys_base=base;
    nova_pmm.hhdm_offset=(uintptr_t)offset;
    nova_pmm.pages=pages;
    nova_pmm.in_use=0;
    nova_pmm.initialized=true;
    return true;
}
/* Trusted-kernel owner lifecycle only. IDs are not authenticated PIDs. */
static bool nova_owner_create(uint64_t *out){
    if(!out)return false;
    *out=0;
    if(!nova_pmm_metadata_valid() || nova_pmm.next_owner==UINT64_MAX)
        return false;
    for(uint32_t i=0;i<NOVA_OWNER_SLOT_CAP;i++){
        if(nova_pmm.owner_registry[i])continue;
        const uint64_t id=nova_pmm.next_owner++;
        nova_pmm.owner_registry[i]=id;
        *out=id;
        return true;
    }
    return false;
}
/* Never retire an owner with any outstanding frame allocation. */
static bool nova_owner_retire(uint64_t owner){
    if(!owner || !nova_pmm_metadata_valid())return false;
    uint32_t slot=NOVA_OWNER_SLOT_CAP;
    for(uint32_t i=0;i<NOVA_OWNER_SLOT_CAP;i++)
        if(nova_pmm.owner_registry[i]==owner){slot=i;break;}
    if(slot==NOVA_OWNER_SLOT_CAP)return false;
    for(uint32_t i=0;i<nova_pmm.pages;i++)
        if(nova_pmm.owners[i]==owner)return false;
    nova_pmm.owner_registry[slot]=0;
    return true;
}
/* Typed allocations use the same PMM bitmap, so roles are globally
 * exclusive for the entire managed physical pool. */
static void *nova_page_alloc_role(enum nova_frame_role role){
    if(!nova_pmm_role_valid(role)||!nova_pmm_metadata_valid()||
       nova_pmm.next_generation==UINT64_MAX)return NULL;
    for(uint32_t i=0;i<nova_pmm.pages;i++){
        uint8_t mask=(uint8_t)(1u<<(i&7));
        if(nova_pmm.bitmap[i>>3]&mask)continue;
        nova_pmm.bitmap[i>>3]|=mask;
        nova_pmm.roles[i]=(uint8_t)role;
        nova_pmm.generations[i]=nova_pmm.next_generation++;
        nova_pmm.owners[i]=0; /* pointer-only allocations are unowned */
        nova_pmm.in_use++;
        uintptr_t mapped=(uintptr_t)(nova_pmm.phys_base+(uint64_t)i*NOVA_PAGE_SIZE)+nova_pmm.hhdm_offset;
        uint8_t *page=(uint8_t*)mapped;
        for(size_t b=0;b<NOVA_PAGE_SIZE;b++)page[b]=0;
        return page;
    }
    return NULL;
}
static void *nova_page_alloc(void){
    return nova_page_alloc_role(NOVA_FRAME_GENERAL);
}
/* Only a matching allocation owner may release a frame. The pointer-only
 * wrapper passes owner 0 and cannot free owner-tagged frames. */
static bool nova_page_free_owned(void *ptr,enum nova_frame_role role,
                                 uint64_t owner){
    if(!nova_pmm_role_valid(role)||!ptr||!nova_pmm_metadata_valid())
        return false;
    uintptr_t mapped=(uintptr_t)ptr;
    uintptr_t beginning=(uintptr_t)nova_pmm.phys_base+nova_pmm.hhdm_offset;
    if(mapped<beginning)return false;
    uintptr_t delta=mapped-beginning;
    if(delta%NOVA_PAGE_SIZE || delta/NOVA_PAGE_SIZE>=nova_pmm.pages)return false;
    uint32_t i=(uint32_t)(delta/NOVA_PAGE_SIZE);
    uint8_t mask=(uint8_t)(1u<<(i&7));
    if(!(nova_pmm.bitmap[i>>3]&mask)||
       nova_pmm.roles[i]!=(uint8_t)role || nova_pmm.owners[i]!=owner)
        return false;
    /* Erase the released page before making it available to another app.
       Volatile stores prevent the compiler from optimizing the wipe out. */
    volatile uint8_t *bytes=(volatile uint8_t *)ptr;
    for(size_t b=0;b<NOVA_PAGE_SIZE;b++)bytes[b]=0;
    nova_pmm.bitmap[i>>3]=(uint8_t)(nova_pmm.bitmap[i>>3]&~mask);
    nova_pmm.roles[i]=NOVA_FRAME_FREE;
    nova_pmm.generations[i]=0;
    nova_pmm.owners[i]=0;
    nova_pmm.in_use--;
    return true;
}
static bool nova_page_free_role(void *ptr,enum nova_frame_role role){
    return nova_page_free_owned(ptr,role,0);
}
static bool nova_page_free(void *ptr){
    return nova_page_free_role(ptr,NOVA_FRAME_GENERAL);
}
/* Convert a PMM-owned HHDM page to its verified physical address.
 * Does not accept arbitrary kernel pointers or unallocated frames. */
static bool nova_pmm_physical(const void *ptr,uint64_t *physical){
    if(!ptr||!physical||!nova_pmm_metadata_valid())return false;
    uintptr_t base=(uintptr_t)nova_pmm.phys_base+nova_pmm.hhdm_offset;
    uintptr_t p=(uintptr_t)ptr;
    if(p<base)return false;
    uintptr_t delta=p-base;
    if((delta&(NOVA_PAGE_SIZE-1u))!=0||
       delta/NOVA_PAGE_SIZE>=nova_pmm.pages)return false;
    uint32_t index=(uint32_t)(delta/NOVA_PAGE_SIZE);
    if(!(nova_pmm.bitmap[index>>3]&(uint8_t)(1u<<(index&7u))) ||
       nova_pmm.owners[index]!=0)return false;
    *physical=nova_pmm.phys_base+(uint64_t)index*NOVA_PAGE_SIZE;
    return true;
}
/* Validate an allocated frame's role independently of caller metadata.
 * An old pointer can still refer to a recycled frame of the SAME role:
 * there is no generation token or per-process owner identity yet. */
static bool nova_pmm_frame_role(const void *ptr,enum nova_frame_role *out){
    if(!out||!ptr||!nova_pmm_metadata_valid())return false;
    const uintptr_t base=(uintptr_t)nova_pmm.phys_base+nova_pmm.hhdm_offset;
    const uintptr_t p=(uintptr_t)ptr;
    if(p<base)return false;
    const uintptr_t delta=p-base;
    if((delta&(NOVA_PAGE_SIZE-1u))!=0||
       delta/NOVA_PAGE_SIZE>=nova_pmm.pages)return false;
    const uint32_t i=(uint32_t)(delta/NOVA_PAGE_SIZE);
    if(!(nova_pmm.bitmap[i>>3]&(uint8_t)(1u<<(i&7u))) ||
       nova_pmm.owners[i]!=0)return false;
    *out=(enum nova_frame_role)nova_pmm.roles[i];
    return true;
}
static inline bool nova_pmm_physical_role(const void *ptr,enum nova_frame_role role,
                                   uint64_t *physical){
    enum nova_frame_role actual=NOVA_FRAME_FREE;
    return nova_pmm_role_valid(role)&&
           nova_pmm_frame_role(ptr,&actual)&&actual==role&&
           nova_pmm_physical(ptr,physical);
}
/* Optional allocation tickets defend against stale pointers when a frame
 * is freed and reallocated with the SAME role. The caller must retain the
 * original ticket; reconstructing a new ticket from an arbitrary pointer
 * would defeat this check. Existing pointer-only callbacks remain legacy
 * and are NOT protected from same-role ABA reuse by this API.
 *
 * An optional nonzero owner ID distinguishes trusted caller allocations.
 * It is NOT authenticated, assigned to a real process, or a defense against
 * a malicious kernel. The PMM remains single-core/non-reentrant.
 */
struct nova_frame_ticket {
    void *frame;
    uint64_t generation;
    enum nova_frame_role role;
    uint64_t owner; /* 0 for legacy tickets */
};
static inline bool nova_pmm_ticket_valid(const struct nova_frame_ticket *ticket){
    if(!ticket || !ticket->frame || !ticket->generation ||
       !nova_pmm_role_valid(ticket->role) ||
       !nova_pmm_metadata_valid())return false;
    const uintptr_t base=(uintptr_t)nova_pmm.phys_base+nova_pmm.hhdm_offset;
    const uintptr_t p=(uintptr_t)ticket->frame;
    if(p<base)return false;
    const uintptr_t delta=p-base;
    if((delta&(NOVA_PAGE_SIZE-1u))!=0 ||
       delta/NOVA_PAGE_SIZE>=nova_pmm.pages)return false;
    const uint32_t i=(uint32_t)(delta/NOVA_PAGE_SIZE);
    return (nova_pmm.bitmap[i>>3]&(uint8_t)(1u<<(i&7u)))!=0 &&
           nova_pmm.roles[i]==(uint8_t)ticket->role &&
           nova_pmm.generations[i]==ticket->generation &&
           nova_pmm.owners[i]==ticket->owner;
}
static inline bool nova_page_alloc_ticket(enum nova_frame_role role,
                                   struct nova_frame_ticket *out){
    if(!out)return false;
    *out=(struct nova_frame_ticket){0};
    void *frame=nova_page_alloc_role(role);
    if(!frame)return false;
    const uintptr_t base=(uintptr_t)nova_pmm.phys_base+nova_pmm.hhdm_offset;
    const uint32_t i=(uint32_t)(((uintptr_t)frame-base)/NOVA_PAGE_SIZE);
    *out=(struct nova_frame_ticket){
        .frame=frame,.generation=nova_pmm.generations[i],.role=role,
        .owner=0
    };
    return true;
}
/* Owner IDs must be active IDs from nova_owner_create(). This does not
 * authenticate callers or establish a real process identity. */
static inline bool nova_page_alloc_owned_ticket(enum nova_frame_role role,
                                  uint64_t owner,struct nova_frame_ticket *out){
    if(!out)return false;
    *out=(struct nova_frame_ticket){0};
    if(!owner || !nova_pmm_role_valid(role) ||
       !nova_pmm_metadata_valid() || !nova_pmm_owner_active(owner))return false;
    void *frame=nova_page_alloc_role(role);
    if(!frame)return false;
    const uintptr_t base=(uintptr_t)nova_pmm.phys_base+nova_pmm.hhdm_offset;
    const uint32_t i=(uint32_t)(((uintptr_t)frame-base)/NOVA_PAGE_SIZE);
    nova_pmm.owners[i]=owner;
    *out=(struct nova_frame_ticket){
        .frame=frame,.generation=nova_pmm.generations[i],.role=role,
        .owner=owner
    };
    return true;
}
static inline bool nova_page_free_ticket(const struct nova_frame_ticket *ticket){
    return nova_pmm_ticket_valid(ticket) &&
           nova_page_free_owned(ticket->frame,ticket->role,ticket->owner);
}
static inline bool nova_pmm_physical_ticket(const struct nova_frame_ticket *ticket,
                                     uint64_t *physical){
    if(!physical || !nova_pmm_ticket_valid(ticket))return false;
    const uintptr_t base=(uintptr_t)nova_pmm.phys_base+nova_pmm.hhdm_offset;
    const uint32_t i=(uint32_t)(((uintptr_t)ticket->frame-base)/NOVA_PAGE_SIZE);
    *physical=nova_pmm.phys_base+(uint64_t)i*NOVA_PAGE_SIZE;
    return true;
}
static uint32_t nova_free_pages(void){
    return nova_pmm_metadata_valid()?nova_pmm.pages-nova_pmm.in_use:0;
}
static bool nova_pmm_self_test(void){
    if(!nova_pmm_metadata_valid())return false;
    uint32_t old=nova_free_pages();
    uint64_t *a=(uint64_t*)nova_page_alloc();
    uint64_t *b=(uint64_t*)nova_page_alloc();
    if(!a || !b || a==b){
        if(a)nova_page_free(a);
        if(b)nova_page_free(b);
        return false;
    }
    bool zero=(*a==0 && *b==0);
    *a=0x4E4F564150414745ull;
    *b=0xC0FFEE1234567890ull;
    bool good=zero && *a==0x4E4F564150414745ull && *b==0xC0FFEE1234567890ull;
    bool freedA=nova_page_free(a);
    bool doubleFreeRejected=!nova_page_free(a);
    bool freedB=nova_page_free(b);
    return good && freedA && freedB && doubleFreeRejected && old==nova_free_pages();
}
#endif
