/* NOVA Native: bounded ELF64 executable validator.
 *
 * This code INSPECTS untrusted ELF images. It does not allocate mapped
 * pages, load an executable into memory, change CR3, execute instructions,
 * create a process or transition to ring 3. No program execution exists.
 *
 * Deliberately supports only ELF64 little-endian x86_64 ET_EXEC with
 * static PT_LOAD segments. Rejects interpreters, dynamic linking, TLS,
 * executable stacks, writable+executable pages, overlap, huge images
 * and all non-user-space addresses. A future loader must independently
 * enforce all permissions and apply the physical-memory mappings.
 *
 * File data is read via the bounded callback (VFS provides this in-kernel).
 */
#ifndef NOVA_ELF64_H
#define NOVA_ELF64_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define NOVA_ELF64_PH_SIZE 56u
#define NOVA_ELF64_EH_SIZE 64u
#define NOVA_ELF64_MAX_PH 64u
#define NOVA_ELF64_MAX_LOAD 16u
#define NOVA_ELF64_MAX_PAGES 4096u
#define NOVA_ELF64_PAGE_SIZE 4096u
#define NOVA_ELF64_USER_MIN UINT64_C(0x0000000000400000)
#define NOVA_ELF64_USER_MAX UINT64_C(0x00007fff00000000)

typedef bool (*nova_elf_read_fn)(void *context,uint64_t offset,
                                 uint8_t *out,size_t length);
enum nova_elf_result {
    NOVA_ELF_OK=0,
    NOVA_ELF_BAD_FORMAT,
    NOVA_ELF_UNSUPPORTED,
    NOVA_ELF_UNSAFE,
    NOVA_ELF_IO_ERROR
};
struct nova_elf_segment {
    uint64_t virtual_address;
    uint64_t memory_size;
    uint64_t file_offset;
    uint64_t file_size;
    uint32_t flags;
};
struct nova_elf_info {
    uint64_t entry;
    uint32_t pages;
    uint16_t load_count;
    struct nova_elf_segment loads[NOVA_ELF64_MAX_LOAD];
};
static uint16_t nova_elf_u16(const uint8_t *b){
    return (uint16_t)((uint16_t)b[0]|((uint16_t)b[1]<<8));
}
static uint32_t nova_elf_u32(const uint8_t *b){
    return (uint32_t)b[0]|((uint32_t)b[1]<<8)|
           ((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24);
}
static uint64_t nova_elf_u64(const uint8_t *b){
    return (uint64_t)nova_elf_u32(b)|((uint64_t)nova_elf_u32(b+4)<<32);
}
static bool nova_elf_read_exact(nova_elf_read_fn reader,void *ctx,
                                uint64_t file_size,uint64_t offset,
                                uint8_t *out,size_t length){
    if(!reader||(!out&&length)||offset>file_size ||
       (uint64_t)length>file_size-offset)return false;
    return reader(ctx,offset,out,length);
}
static bool nova_elf_user_range(uint64_t start,uint64_t length){
    return length>0 && start>=NOVA_ELF64_USER_MIN &&
           start<NOVA_ELF64_USER_MAX &&
           length<=NOVA_ELF64_USER_MAX-start;
}
static bool nova_elf_overlap(uint64_t a,uint64_t alen,
                             uint64_t b,uint64_t blen){
    return a<b+blen && b<a+alen;
}
static const char *nova_elf_result_name(enum nova_elf_result r){
    switch(r){
        case NOVA_ELF_OK: return "VALID STATIC ELF64 (NOT EXECUTED)";
        case NOVA_ELF_BAD_FORMAT: return "INVALID OR TRUNCATED ELF FILE";
        case NOVA_ELF_UNSUPPORTED: return "UNSUPPORTED ELF TYPE OR ARCH";
        case NOVA_ELF_UNSAFE: return "UNSAFE ELF MEMORY LAYOUT";
        case NOVA_ELF_IO_ERROR: return "ELF FILE READ ERROR";
        default: return "UNKNOWN ELF ERROR";
    }
}
static enum nova_elf_result nova_elf64_probe(nova_elf_read_fn reader,
        void *context,uint64_t file_size,struct nova_elf_info *out){
    if(!out)return NOVA_ELF_BAD_FORMAT;
    *out=(struct nova_elf_info){0};
    if(!reader||file_size<NOVA_ELF64_EH_SIZE)return NOVA_ELF_BAD_FORMAT;
    uint8_t header[NOVA_ELF64_EH_SIZE];
    if(!nova_elf_read_exact(reader,context,file_size,0,header,sizeof(header)))
        return NOVA_ELF_IO_ERROR;
    if(header[0]!=0x7f||header[1]!='E'||header[2]!='L'||header[3]!='F')
        return NOVA_ELF_BAD_FORMAT;
    if(header[4]!=2||header[5]!=1||header[6]!=1 ||
       (header[7]!=0&&header[7]!=3) || /* SYSV or Linux OSABI */
       nova_elf_u16(header+18)!=62 ||
       nova_elf_u16(header+16)!=2)  /* only ET_EXEC, not ET_DYN/PIE */
        return NOVA_ELF_UNSUPPORTED;
    if(nova_elf_u32(header+20)!=1 ||
       nova_elf_u16(header+52)!=NOVA_ELF64_EH_SIZE ||
       nova_elf_u16(header+54)!=NOVA_ELF64_PH_SIZE)
        return NOVA_ELF_BAD_FORMAT;
    uint16_t count=nova_elf_u16(header+56);
    if(count==0||count>NOVA_ELF64_MAX_PH)
        return NOVA_ELF_UNSUPPORTED;
    uint64_t phoff=nova_elf_u64(header+32);
    uint64_t table_bytes=(uint64_t)count*NOVA_ELF64_PH_SIZE;
    if(phoff<NOVA_ELF64_EH_SIZE||phoff>file_size ||
       table_bytes>file_size-phoff)return NOVA_ELF_BAD_FORMAT;
    uint64_t entry=nova_elf_u64(header+24);
    if(!nova_elf_user_range(entry,1))return NOVA_ELF_UNSAFE;
    out->entry=entry;

    bool entry_is_executable=false;
    for(uint16_t i=0;i<count;i++){
        uint8_t ph[NOVA_ELF64_PH_SIZE];
        if(!nova_elf_read_exact(reader,context,file_size,
             phoff+(uint64_t)i*NOVA_ELF64_PH_SIZE,ph,sizeof(ph)))
            return NOVA_ELF_IO_ERROR;
        uint32_t type=nova_elf_u32(ph),flags=nova_elf_u32(ph+4);
        if(type==3||type==2||type==7) /* INTERP, DYNAMIC, TLS */
            return NOVA_ELF_UNSUPPORTED;
        if(type==0x6474e551u && (flags&1u)) /* GNU_STACK executable */
            return NOVA_ELF_UNSAFE;
        if(type!=1)continue; /* PT_LOAD only */
        if(out->load_count>=NOVA_ELF64_MAX_LOAD)return NOVA_ELF_UNSUPPORTED;
        if((flags&~7u)!=0 || (flags&2u && flags&1u))
            return NOVA_ELF_UNSAFE; /* W+X forbidden */
        uint64_t offset=nova_elf_u64(ph+8);
        uint64_t va=nova_elf_u64(ph+16);
        uint64_t filesz=nova_elf_u64(ph+32);
        uint64_t memsz=nova_elf_u64(ph+40);
        uint64_t align=nova_elf_u64(ph+48);
        if(memsz<filesz||!nova_elf_user_range(va,memsz) ||
           offset>file_size || filesz>file_size-offset)
            return NOVA_ELF_BAD_FORMAT;
        if(align<NOVA_ELF64_PAGE_SIZE || (align&(align-1)) ||
           (va&(align-1))!=(offset&(align-1)))
            return NOVA_ELF_UNSAFE;
        /* A page-based loader must not give two segments conflicting rights.
           Reject page overlap even when the byte ranges are disjoint. */
        uint64_t begin=va & ~(UINT64_C(4095));
        uint64_t end=va+memsz;
        uint64_t aligned_end=(end+NOVA_ELF64_PAGE_SIZE-1u)&~UINT64_C(4095);
        if(aligned_end<end)return NOVA_ELF_UNSAFE;
        uint64_t span=aligned_end-begin;
        for(uint16_t j=0;j<out->load_count;j++){
            const struct nova_elf_segment *prev=&out->loads[j];
            uint64_t pbeg=prev->virtual_address & ~UINT64_C(4095);
            uint64_t pend=(prev->virtual_address+prev->memory_size+
                NOVA_ELF64_PAGE_SIZE-1u)&~UINT64_C(4095);
            if(nova_elf_overlap(begin,span,pbeg,pend-pbeg))
                return NOVA_ELF_UNSAFE;
        }
        uint64_t pages=span/NOVA_ELF64_PAGE_SIZE;
        if(pages>NOVA_ELF64_MAX_PAGES-out->pages)return NOVA_ELF_UNSAFE;
        out->pages+=(uint32_t)pages;
        struct nova_elf_segment *dest=&out->loads[out->load_count++];
        dest->virtual_address=va;dest->memory_size=memsz;
        dest->file_offset=offset;dest->file_size=filesz;dest->flags=flags;
        if((flags&1u)&&entry>=va && entry-va<filesz)
            entry_is_executable=true;
    }
    if(!entry_is_executable||out->load_count==0)return NOVA_ELF_UNSAFE;
    return NOVA_ELF_OK;
}
#endif
