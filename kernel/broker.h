/* NOVA OS — offline system-call broker contract (ABI draft v1).
 *
 * Developer-only, kernel-mode request dispatcher. THERE IS NO SYSCALL TRAP
 * and no user-mode execution. A future ring-3 syscall entry must COPY IN
 * arguments from a verified user address space, derive PID from the trusted
 * scheduler (NEVER from a user argument), dispatch here, and COPY OUT.
 *
 * The input/output structures contain only fixed-size inline byte arrays:
 * no raw caller-provided pointers, no unbounded strings, no dynamic memory.
 * All file operations go through the staged app capability policy.
 *
 * NO network, microphone, hardware, disk-write or privileged operations.
 */
#ifndef NOVA_BROKER_H
#define NOVA_BROKER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "capabilities.h"

#define NOVA_BROKER_ABI_VERSION 1u
#define NOVA_BROKER_MAX_BYTES 64u
#define NOVA_BROKER_PATH_CAP 32u

enum nova_broker_operation {
    NOVA_BROKER_VERSION=1,
    NOVA_BROKER_READ=2,
    NOVA_BROKER_WRITE_RAM=3,
    NOVA_BROKER_STAT=4
};
enum nova_broker_status {
    NOVA_BROKER_OK=0,
    NOVA_BROKER_INVALID_REQUEST,
    NOVA_BROKER_DENIED,
    NOVA_BROKER_IO_ERROR
};
/* Never import a PID from this untrusted request structure. */
struct nova_broker_request {
    uint32_t operation;
    uint32_t length;
    uint64_t offset;
    char path[NOVA_BROKER_PATH_CAP];
    uint8_t bytes[NOVA_BROKER_MAX_BYTES];
};
struct nova_broker_response {
    enum nova_broker_status status;
    uint32_t version;
    uint32_t length;
    uint32_t file_size;
    bool read_only;
    uint8_t bytes[NOVA_BROKER_MAX_BYTES];
};
struct nova_broker_context {
    struct nova_cap_registry *rights;
    struct nova_process_table *processes;
    const struct nova_vfs *vfs;
};

/* This is not a zero-copy or an asynchronous syscall. A kernel copies request
 * bytes into its own bounded stack-local object before reaching this API.
 * For now, the only callers are the developer terminal and host tests.
 */
static inline bool nova_broker_path_valid(const char path[NOVA_BROKER_PATH_CAP]) {
    if(!path)return false;
    size_t end=0;
    while(end<NOVA_BROKER_PATH_CAP && path[end])end++;
    if(end==0||end==NOVA_BROKER_PATH_CAP)return false;
    /* Reject any bytes following the NUL to avoid ambiguous/tricky requests. */
    for(size_t i=end+1;i<NOVA_BROKER_PATH_CAP;i++)
        if(path[i]!=0)return false;
    enum nova_vfs_volume volume=NOVA_VFS_NONE;
    const char *leaf=NULL;
    return nova_vfs_prefix(path,&volume,&leaf)&&nova_vfs_name(leaf);
}
static inline enum nova_broker_status nova_broker_dispatch(
    const struct nova_broker_context *context,uint32_t trusted_pid,
    const struct nova_broker_request *request,
    struct nova_broker_response *response) {
    if(!response)return NOVA_BROKER_INVALID_REQUEST;
    /* Clear all output on every call, including failures. */
    *response=(struct nova_broker_response){0};
    response->status=NOVA_BROKER_INVALID_REQUEST;
    if(!context||!context->rights||!context->processes||!context->vfs||
       !request||trusted_pid==0)return response->status;
    if(!nova_cap_lookup(context->rights,context->processes,trusted_pid)){
        response->status=NOVA_BROKER_DENIED;
        return response->status;
    }
    if(request->operation==NOVA_BROKER_VERSION){
        if(request->length!=0||request->offset!=0){
            response->status=NOVA_BROKER_INVALID_REQUEST;
            return response->status;
        }
        response->version=NOVA_BROKER_ABI_VERSION;
        response->status=NOVA_BROKER_OK;
        return response->status;
    }
    if(request->operation!=NOVA_BROKER_READ &&
       request->operation!=NOVA_BROKER_WRITE_RAM &&
       request->operation!=NOVA_BROKER_STAT)return response->status;
    if(!nova_broker_path_valid(request->path))return response->status;
    if(request->length>NOVA_BROKER_MAX_BYTES)return response->status;
    if((request->operation==NOVA_BROKER_READ||
        request->operation==NOVA_BROKER_WRITE_RAM)&&request->length==0)
        return response->status;
    if(request->operation==NOVA_BROKER_STAT &&
       (request->length!=0 || request->offset!=0))
        return response->status;
    if(request->offset>SIZE_MAX)return response->status;
    if(!nova_cap_path_check(context->rights,context->processes,trusted_pid,
         request->path,request->operation==NOVA_BROKER_WRITE_RAM)){
        response->status=NOVA_BROKER_DENIED;
        return response->status;
    }
    if(request->operation==NOVA_BROKER_STAT){
        uint32_t file_size=0;
        bool read_only=false;
        if(!nova_vfs_size(context->vfs,request->path,&file_size,&read_only)){
            response->status=NOVA_BROKER_IO_ERROR;
            return response->status;
        }
        response->file_size=file_size;
        response->read_only=read_only;
        response->status=NOVA_BROKER_OK;
        return response->status;
    }
    if(request->operation==NOVA_BROKER_WRITE_RAM){
        /* No offsets: the current RAMFS write operation replaces entire data. */
        if(request->offset!=0)return response->status;
        if(!nova_cap_write_ram(context->rights,context->processes,trusted_pid,
             context->vfs,request->path,request->bytes,request->length)){
            response->status=NOVA_BROKER_IO_ERROR;
            return response->status;
        }
        response->length=request->length;
        response->status=NOVA_BROKER_OK;
        return response->status;
    }
    /* Never return partial or stale data when reading fails. */
    uint8_t scratch[NOVA_BROKER_MAX_BYTES]={0};
    if(!nova_cap_read_file(context->rights,context->processes,trusted_pid,
         context->vfs,request->path,(size_t)request->offset,scratch,
         request->length)){
        response->status=NOVA_BROKER_IO_ERROR;
        return response->status;
    }
    for(uint32_t i=0;i<request->length;i++)response->bytes[i]=scratch[i];
    response->length=request->length;
    response->status=NOVA_BROKER_OK;
    return response->status;
}
#endif
