/* NOVA x86-64 kernel exception diagnostics.
 *
 * Ring-0 diagnostic frame produced by irq.S. No user-mode stack slots
 * are accessed; RIP/CS/RFLAGS exist for same-privilege exceptions.
 * Fatal handler only, no attempt to resume, no syscall or user isolation.
 */
#ifndef NOVA_FAULT_H
#define NOVA_FAULT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct nova_fault_frame {
    uint64_t r15,r14,r13,r12,r11,r10,r9,r8;
    uint64_t rdi,rsi,rbp,rdx,rcx,rbx,rax;
    uint64_t vector,error_code,rip,cs,rflags;
};
/* Must correspond exactly to the assembly save/interrupt-frame layout. */
_Static_assert(sizeof(struct nova_fault_frame)==20u*8u,
               "Incorrect kernel fault frame layout");
_Static_assert(offsetof(struct nova_fault_frame,r15)==0u,
               "Fault R15 offset changed");
_Static_assert(offsetof(struct nova_fault_frame,r14)==8u,
               "Fault R14 offset changed");
_Static_assert(offsetof(struct nova_fault_frame,r13)==16u,
               "Fault R13 offset changed");
_Static_assert(offsetof(struct nova_fault_frame,r12)==24u,
               "Fault R12 offset changed");
_Static_assert(offsetof(struct nova_fault_frame,r11)==32u,
               "Fault R11 offset changed");
_Static_assert(offsetof(struct nova_fault_frame,r10)==40u,
               "Fault R10 offset changed");
_Static_assert(offsetof(struct nova_fault_frame,r9)==48u,
               "Fault R9 offset changed");
_Static_assert(offsetof(struct nova_fault_frame,r8)==56u,
               "Fault R8 offset changed");
_Static_assert(offsetof(struct nova_fault_frame,rdi)==64u,
               "Fault RDI offset changed");
_Static_assert(offsetof(struct nova_fault_frame,rsi)==72u,
               "Fault RSI offset changed");
_Static_assert(offsetof(struct nova_fault_frame,rbp)==80u,
               "Fault RBP offset changed");
_Static_assert(offsetof(struct nova_fault_frame,rdx)==88u,
               "Fault RDX offset changed");
_Static_assert(offsetof(struct nova_fault_frame,rcx)==96u,
               "Fault RCX offset changed");
_Static_assert(offsetof(struct nova_fault_frame,rbx)==104u,
               "Fault RBX offset changed");
_Static_assert(offsetof(struct nova_fault_frame,rax)==112u,
               "Fault RAX offset changed");
_Static_assert(offsetof(struct nova_fault_frame,vector)==120u,
               "Fault vector offset changed");
_Static_assert(offsetof(struct nova_fault_frame,error_code)==128u,
               "Fault error-code offset changed");
_Static_assert(offsetof(struct nova_fault_frame,rip)==136u,
               "Fault RIP offset changed");
_Static_assert(offsetof(struct nova_fault_frame,cs)==144u,
               "Fault CS offset changed");
_Static_assert(offsetof(struct nova_fault_frame,rflags)==152u,
               "Fault RFLAGS offset changed");

static inline bool nova_fault_has_cpu_error(unsigned vector){
    switch(vector){
        case 8:  /* double fault */
        case 10: /* invalid TSS */
        case 11: /* segment not present */
        case 12: /* stack segment */
        case 13: /* general protection */
        case 14: /* page fault */
        case 17: /* alignment check */
        case 21: /* control protection */
        case 29: /* VMM communication */
        case 30: /* security exception */
            return true;
        default:return false;
    }
}
static inline const char *nova_fault_name(uint64_t vector){
    switch(vector){
        case 0:return "DIVIDE_ERROR";
        case 1:return "DEBUG";
        case 2:return "NONMASKABLE_INTERRUPT";
        case 3:return "BREAKPOINT";
        case 4:return "OVERFLOW";
        case 5:return "BOUND_RANGE";
        case 6:return "INVALID_OPCODE";
        case 7:return "DEVICE_NOT_AVAILABLE";
        case 8:return "DOUBLE_FAULT";
        case 9:return "COPROCESSOR_SEGMENT_OVERRUN";
        case 10:return "INVALID_TSS";
        case 11:return "SEGMENT_NOT_PRESENT";
        case 12:return "STACK_SEGMENT_FAULT";
        case 13:return "GENERAL_PROTECTION";
        case 14:return "PAGE_FAULT";
        case 15:return "RESERVED";
        case 16:return "X87_FLOATING_POINT";
        case 17:return "ALIGNMENT_CHECK";
        case 18:return "MACHINE_CHECK";
        case 19:return "SIMD_FLOATING_POINT";
        case 20:return "VIRTUALIZATION";
        case 21:return "CONTROL_PROTECTION";
        case 28:return "HYPERVISOR_INJECTION";
        case 29:return "VMM_COMMUNICATION";
        case 30:return "SECURITY_EXCEPTION";
        default:return "UNKNOWN_EXCEPTION";
    }
}
struct nova_fault_writer {
    char *out;
    size_t capacity;
    size_t written;
    bool truncated;
};
static inline void nova_fault_put(struct nova_fault_writer *w,char c){
    if(w->capacity && w->written<w->capacity-1u)w->out[w->written]=c;
    else w->truncated=true;
    if(w->written<SIZE_MAX)w->written++;
    else w->truncated=true;
}
static inline void nova_fault_text(struct nova_fault_writer *w,const char *s){
    for(;*s;s++)nova_fault_put(w,*s);
}
static inline void nova_fault_hex(struct nova_fault_writer *w,uint64_t n){
    static const char hex[]="0123456789ABCDEF";
    nova_fault_text(w,"0x");
    for(int shift=60;shift>=0;shift-=4)
        nova_fault_put(w,hex[(n>>(unsigned)shift)&15u]);
}
/* Returns false if the output did not fit. Even then the buffer is NUL
 * terminated whenever capacity > 0, never written beyond its boundary.
 */
static inline bool nova_fault_format(const struct nova_fault_frame *frame,
    uint64_t cr2,char *output,size_t capacity){
    if(!frame||(!output&&capacity))return false;
    struct nova_fault_writer w={.out=output,.capacity=capacity,.written=0,
                                 .truncated=false};
    nova_fault_text(&w,"NOVA_EXCEPTION_FATAL vector=");
    nova_fault_hex(&w,frame->vector);
    nova_fault_text(&w," name=");
    nova_fault_text(&w,nova_fault_name(frame->vector));
    nova_fault_text(&w," error=");
    nova_fault_hex(&w,frame->error_code);
    nova_fault_text(&w," rip=");
    nova_fault_hex(&w,frame->rip);
    nova_fault_text(&w," cs=");
    nova_fault_hex(&w,frame->cs);
    nova_fault_text(&w," rflags=");
    nova_fault_hex(&w,frame->rflags);
    if(frame->vector==14){
        nova_fault_text(&w," cr2=");
        nova_fault_hex(&w,cr2);
    }
    if(capacity){
        const size_t last=w.written<capacity?w.written:capacity-1u;
        output[last]=0;
    }
    return !w.truncated;
}
#endif
