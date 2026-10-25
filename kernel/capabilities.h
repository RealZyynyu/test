/* NOVA staged-app capability policy, developer prototype.
 *
 * Security design (NOT a working app sandbox):
 * - All new staged images start with ZERO rights (default deny).
 * - Permissions are explicitly associated with a PID and bounded slot.
 * - A trusted kernel caller can grant/revoke *implemented file rights only*.
 * - IDs are re-checked against the process image table to avoid stale grants.
 * - READ and WRITE paths are volume-scoped; DISK is *never* writable.
 * - Protected RAM entries are not writable through this app-facing wrapper.
 * - Other future rights (network, controller, microphone) are reserved and
 *   cannot be granted before actual drivers and isolation exist.
 *
 * LIMITATION: NOVA still executes everything in ring 0; these wrappers do
 * NOT restrict direct kernel access, and they are not syscalls. A future
 * ring-3 runtime must route every app request through a privileged broker
 * after real page-table and process isolation is in place.
 */
#ifndef NOVA_CAPABILITIES_H
#define NOVA_CAPABILITIES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "process_image.h"
#include "vfs.h"

#define NOVA_CAP_RAM_READ   (UINT32_C(1)<<0)
#define NOVA_CAP_RAM_WRITE  (UINT32_C(1)<<1)
#define NOVA_CAP_DISK_READ  (UINT32_C(1)<<2)
#define NOVA_CAP_NETWORK    (UINT32_C(1)<<3)
#define NOVA_CAP_MICROPHONE (UINT32_C(1)<<4)
#define NOVA_CAP_CONTROLLER (UINT32_C(1)<<5)
#define NOVA_CAP_APP_SAVE   (UINT32_C(1)<<6)

/* File rights and private RAM-save access have mediation wrappers. */
#define NOVA_CAP_IMPLEMENTED (NOVA_CAP_RAM_READ|NOVA_CAP_RAM_WRITE|NOVA_CAP_DISK_READ|NOVA_CAP_APP_SAVE)

struct nova_cap_entry {
    uint32_t pid;
    uint32_t granted;
};
struct nova_cap_registry {
    struct nova_cap_entry slots[NOVA_PROCESS_SLOTS];
};
static inline bool nova_cap_mask_valid(uint32_t mask) {
    return mask!=0 && (mask & ~NOVA_CAP_IMPLEMENTED)==0;
}
static inline struct nova_cap_entry *nova_cap_lookup(
    struct nova_cap_registry *registry,struct nova_process_table *processes,
    uint32_t pid) {
    if(!registry||!processes||pid==0||!nova_process_find(processes,pid))
        return NULL;
    for(size_t i=0;i<NOVA_PROCESS_SLOTS;i++)
        if(registry->slots[i].pid==pid)return &registry->slots[i];
    return NULL;
}
static inline bool nova_cap_register(
    struct nova_cap_registry *registry,struct nova_process_table *processes,
    uint32_t pid) {
    if(!registry||!processes||!pid||!nova_process_find(processes,pid))
        return false;
    if(nova_cap_lookup(registry,processes,pid))return false;
    for(size_t i=0;i<NOVA_PROCESS_SLOTS;i++){
        if(registry->slots[i].pid)continue;
        registry->slots[i]=(struct nova_cap_entry){.pid=pid,.granted=0};
        return true;
    }
    return false;
}
/* Explicitly releases a slot after process teardown, without trusting a
 * stale descriptor. May also be used for a failed staging transaction. */
static inline bool nova_cap_unregister(struct nova_cap_registry *registry,
                                        uint32_t pid) {
    if(!registry||!pid)return false;
    for(size_t i=0;i<NOVA_PROCESS_SLOTS;i++){
        if(registry->slots[i].pid!=pid)continue;
        registry->slots[i]=(struct nova_cap_entry){0};
        return true;
    }
    return false;
}
/* Called only by trusted kernel policy decisions. There is intentionally
 * no corresponding shell command that can grant applications access. */
static inline bool nova_cap_grant_trusted(
    struct nova_cap_registry *registry,struct nova_process_table *processes,
    uint32_t pid,uint32_t mask) {
    if(!nova_cap_mask_valid(mask))return false;
    struct nova_cap_entry *entry=nova_cap_lookup(registry,processes,pid);
    if(!entry)return false;
    entry->granted|=mask;
    return true;
}
static inline bool nova_cap_revoke_trusted(
    struct nova_cap_registry *registry,struct nova_process_table *processes,
    uint32_t pid,uint32_t mask) {
    if(!nova_cap_mask_valid(mask))return false;
    struct nova_cap_entry *entry=nova_cap_lookup(registry,processes,pid);
    if(!entry)return false;
    entry->granted&=~mask;
    return true;
}
static inline bool nova_cap_check(
    struct nova_cap_registry *registry,struct nova_process_table *processes,
    uint32_t pid,uint32_t mask) {
    if(!nova_cap_mask_valid(mask))return false;
    struct nova_cap_entry *entry=nova_cap_lookup(registry,processes,pid);
    return entry && (entry->granted & mask)==mask;
}
static inline uint32_t nova_cap_current(
    struct nova_cap_registry *registry,struct nova_process_table *processes,
    uint32_t pid) {
    struct nova_cap_entry *entry=nova_cap_lookup(registry,processes,pid);
    return entry?entry->granted:0;
}
static inline uint32_t nova_cap_for_volume(enum nova_vfs_volume vol,bool write) {
    if(write)return vol==NOVA_VFS_RAM?NOVA_CAP_RAM_WRITE:0;
    if(vol==NOVA_VFS_RAM)return NOVA_CAP_RAM_READ;
    if(vol==NOVA_VFS_DISK)return NOVA_CAP_DISK_READ;
    return 0;
}
static inline bool nova_cap_path_check(
    struct nova_cap_registry *registry,struct nova_process_table *processes,
    uint32_t pid,const char *path,bool write) {
    enum nova_vfs_volume volume;
    const char *leaf=NULL;
    if(!nova_vfs_prefix(path,&volume,&leaf)||!nova_vfs_name(leaf))
        return false;
    uint32_t required=nova_cap_for_volume(volume,write);
    return required!=0 && nova_cap_check(registry,processes,pid,required);
}
static inline bool nova_cap_read_file(
    struct nova_cap_registry *registry,struct nova_process_table *processes,
    uint32_t pid,const struct nova_vfs *vfs,const char *path,
    size_t offset,uint8_t *dst,size_t length) {
    return vfs && nova_cap_path_check(registry,processes,pid,path,false) &&
           nova_vfs_read(vfs,path,offset,dst,length);
}
static inline bool nova_cap_write_ram(
    struct nova_cap_registry *registry,struct nova_process_table *processes,
    uint32_t pid,const struct nova_vfs *vfs,const char *path,
    const uint8_t *data,size_t length) {
    if(!vfs||!nova_cap_path_check(registry,processes,pid,path,true))
        return false;
    /* The shared RAMFS has no native per-user owner semantics yet. Explicitly
     * prevent app-facing writes to the three protected kernel sample files. */
    const char *leaf=NULL;
    enum nova_vfs_volume volume=NOVA_VFS_NONE;
    if(!nova_vfs_prefix(path,&volume,&leaf)||volume!=NOVA_VFS_RAM)
        return false;
    struct nova_file *f=nova_file_find(leaf);
    if(!f||f->protected)return false;
    return nova_vfs_write(vfs,path,data,length);
}
#endif
