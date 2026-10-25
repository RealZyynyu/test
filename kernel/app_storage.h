/* NOVA OS: per-staged-program VOLATILE save blob.
 *
 * One in-memory save (up to 64 bytes) per currently staged image.
 * PID ownership is checked against the process table on EVERY operation;
 * a game cannot read a different PID's blob through these APIs.
 * New images start empty. Unregister securely clears the entire slot.
 *
 * LIMITS: This is NOT persistent storage, disk encryption, a true sandbox,
 * or hardware-enforced per-process privacy. All code is currently ring 0;
 * a future user-mode syscall broker must supply the authenticated caller PID.
 * No global save list/read API is exposed to application callers.
 */
#ifndef NOVA_APP_STORAGE_H
#define NOVA_APP_STORAGE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "capabilities.h"

#define NOVA_SAVE_CAPACITY 64u

struct nova_save_slot {
    uint32_t owner_pid;
    uint8_t size;
    bool present;
    uint8_t data[NOVA_SAVE_CAPACITY];
};
struct nova_save_store {
    struct nova_save_slot slots[NOVA_PROCESS_SLOTS];
};

static inline struct nova_save_slot *nova_save_find(
    struct nova_save_store *store,struct nova_process_table *processes,
    uint32_t trusted_pid){
    if(!store||!processes||!trusted_pid||
       !nova_process_find(processes,trusted_pid))return NULL;
    for(size_t i=0;i<NOVA_PROCESS_SLOTS;i++)
        if(store->slots[i].owner_pid==trusted_pid)
            return &store->slots[i];
    return NULL;
}
static inline bool nova_save_register(
    struct nova_save_store *store,struct nova_process_table *processes,
    uint32_t trusted_pid){
    if(!store||!processes||!trusted_pid||
       !nova_process_find(processes,trusted_pid)||
       nova_save_find(store,processes,trusted_pid))return false;
    for(size_t i=0;i<NOVA_PROCESS_SLOTS;i++){
        if(store->slots[i].owner_pid)continue;
        store->slots[i]=(struct nova_save_slot){0};
        store->slots[i].owner_pid=trusted_pid;
        return true;
    }
    return false;
}
static inline bool nova_save_unregister(struct nova_save_store *store,
                                         uint32_t trusted_pid){
    if(!store||!trusted_pid)return false;
    for(size_t i=0;i<NOVA_PROCESS_SLOTS;i++){
        if(store->slots[i].owner_pid!=trusted_pid)continue;
        /* Zero contents before returning a slot to the pool. */
        volatile uint8_t *bytes=(volatile uint8_t *)&store->slots[i];
        for(size_t b=0;b<sizeof(store->slots[i]);b++)bytes[b]=0;
        return true;
    }
    return false;
}
/* Requires an explicit app-save capability (not granted by default). */
static inline bool nova_save_replace(
    struct nova_save_store *store,struct nova_cap_registry *registry,
    struct nova_process_table *processes,uint32_t trusted_pid,
    const uint8_t *source,size_t length){
    if(length>NOVA_SAVE_CAPACITY||(!source&&length))return false;
    if(!nova_cap_check(registry,processes,trusted_pid,NOVA_CAP_APP_SAVE))
        return false;
    struct nova_save_slot *slot=nova_save_find(store,processes,trusted_pid);
    if(!slot)return false;
    /* Full overwrite and tail clearing: no data from a previous save leaks. */
    for(size_t i=0;i<NOVA_SAVE_CAPACITY;i++)
        slot->data[i]=i<length?source[i]:0;
    slot->size=(uint8_t)length;
    slot->present=true;
    return true;
}
static inline bool nova_save_read(
    struct nova_save_store *store,struct nova_cap_registry *registry,
    struct nova_process_table *processes,uint32_t trusted_pid,
    size_t offset,uint8_t *dest,size_t length){
    if(length>NOVA_SAVE_CAPACITY||(!dest&&length))return false;
    if(!nova_cap_check(registry,processes,trusted_pid,NOVA_CAP_APP_SAVE))
        return false;
    struct nova_save_slot *slot=nova_save_find(store,processes,trusted_pid);
    if(!slot||!slot->present||offset>slot->size||
       length>(size_t)slot->size-offset)return false;
    for(size_t i=0;i<length;i++)dest[i]=slot->data[offset+i];
    return true;
}
static inline bool nova_save_size(
    struct nova_save_store *store,struct nova_cap_registry *registry,
    struct nova_process_table *processes,uint32_t trusted_pid,
    uint32_t *size){
    if(!size||!nova_cap_check(registry,processes,trusted_pid,NOVA_CAP_APP_SAVE))
        return false;
    struct nova_save_slot *slot=nova_save_find(store,processes,trusted_pid);
    if(!slot||!slot->present)return false;
    *size=slot->size;
    return true;
}
#endif
