/* NOVA offline user-buffer validator for future copyin/copyout.
 *
 * Checks candidate x86-64 PTEs only; never reads or writes untrusted memory,
 * changes page tables or executes instructions. NOT a real user-copy routine.
 *
 * Future syscalls require a trusted kernel CR3 mapping, fault-safe copy
 * operation, time-of-check/time-of-use safety and actual ring-3 enforcement.
 */
#ifndef NOVA_USER_BUFFER_H
#define NOVA_USER_BUFFER_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "user_space.h"

#define NOVA_USER_BUFFER_MAX 4096u

static inline bool nova_user_span_accessible(
        const struct nova_user_space *space,uint64_t address,
        size_t length,bool kernel_writes_to_user){
    if(!space||!space->prepared||length==0||
       length>NOVA_USER_BUFFER_MAX||
       address<NOVA_ELF64_USER_MIN||address>=NOVA_ELF64_USER_MAX)
        return false;
    /* Subtract before adding to avoid wrapping user-controlled pointers. */
    if((uint64_t)length>NOVA_ELF64_USER_MAX-address)
        return false;
    uint64_t last=address+(uint64_t)length-1u;
    uint64_t first_page=address & ~UINT64_C(4095);
    uint64_t last_page=last & ~UINT64_C(4095);
    for(uint64_t page=first_page;page<=last_page;page+=4096u){
        uint64_t frame=0,flags=0;
        if(!nova_pt_lookup(&space->page_tables,page,&frame,&flags))
            return false;
        if((flags&(NOVA_PT_P|NOVA_PT_US))!=(NOVA_PT_P|NOVA_PT_US))
            return false;
        if(kernel_writes_to_user && !(flags&NOVA_PT_RW))
            return false;
    }
    return true;
}
#endif
