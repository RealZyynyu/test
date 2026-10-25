/* NOVA Native x86-64 IRQ entry stubs.
 * Kernel-only, ring-0. Clang builds with -mno-sse/-mno-red-zone.
 * We preserve all GPRs and align the stack before calling C.
 */
.section .text
.p2align 4
.global nova_irq_timer_stub
.type nova_irq_timer_stub,@function
.extern nova_timer_interrupt
nova_irq_timer_stub:
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp,%r12
    andq $-16,%rsp
    call nova_timer_interrupt
    movq %r12,%rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    iretq
.size nova_irq_timer_stub, .-nova_irq_timer_stub

.global nova_irq_master_stub
.type nova_irq_master_stub,@function
nova_irq_master_stub:
    pushq %rax
    pushq %rdx
    movb $0x20,%al
    outb %al,$0x20
    popq %rdx
    popq %rax
    iretq
.size nova_irq_master_stub, .-nova_irq_master_stub

.global nova_irq_slave_stub
.type nova_irq_slave_stub,@function
nova_irq_slave_stub:
    pushq %rax
    pushq %rdx
    movb $0x20,%al
    outb %al,$0xA0
    outb %al,$0x20
    popq %rdx
    popq %rax
    iretq
.size nova_irq_slave_stub, .-nova_irq_slave_stub

/* Exception stubs:
 * CPU pushes an error code for some faults; we add a synthetic zero
 * ONLY for exceptions without a CPU-pushed code.
 * Common stack after saving GPRs:
 *   r15..rax, vector, error_code, RIP, CS, RFLAGS.
 * Do not read RSP/SS: same-ring frames need not contain those fields.
 * This is still a fatal kernel-only path, no IST or ring-3 support.
 */
.macro NOVA_EXC_NOERR n
.global nova_exception_\n
.type nova_exception_\n,@function
nova_exception_\n:
    pushq $0
    pushq $\n
    jmp nova_exception_common
.size nova_exception_\n, .-nova_exception_\n
.endm

.macro NOVA_EXC_ERR n
.global nova_exception_\n
.type nova_exception_\n,@function
nova_exception_\n:
    /* CPU supplied the code below RIP/CS/RFLAGS. */
    pushq $\n
    jmp nova_exception_common
.size nova_exception_\n, .-nova_exception_\n
.endm

NOVA_EXC_NOERR 0
NOVA_EXC_NOERR 1
NOVA_EXC_NOERR 2
NOVA_EXC_NOERR 3
NOVA_EXC_NOERR 4
NOVA_EXC_NOERR 5
NOVA_EXC_NOERR 6
NOVA_EXC_NOERR 7
NOVA_EXC_ERR 8
NOVA_EXC_NOERR 9
NOVA_EXC_ERR 10
NOVA_EXC_ERR 11
NOVA_EXC_ERR 12
NOVA_EXC_ERR 13
NOVA_EXC_ERR 14
NOVA_EXC_NOERR 15
NOVA_EXC_NOERR 16
NOVA_EXC_ERR 17
NOVA_EXC_NOERR 18
NOVA_EXC_NOERR 19
NOVA_EXC_NOERR 20
NOVA_EXC_ERR 21
NOVA_EXC_NOERR 22
NOVA_EXC_NOERR 23
NOVA_EXC_NOERR 24
NOVA_EXC_NOERR 25
NOVA_EXC_NOERR 26
NOVA_EXC_NOERR 27
NOVA_EXC_NOERR 28
NOVA_EXC_ERR 29
NOVA_EXC_ERR 30
NOVA_EXC_NOERR 31

/* Unknown non-legacy vector: report 255, then halt. */
.global nova_exception_stub
.type nova_exception_stub,@function
nova_exception_stub:
    pushq $0
    pushq $255
    jmp nova_exception_common
.size nova_exception_stub, .-nova_exception_stub

.type nova_exception_common,@function
.extern nova_exception_panic
nova_exception_common:
    cli
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    /* The first argument is a pointer to our exact saved-frame layout. */
    movq %rsp,%rdi
    andq $-16,%rsp
    call nova_exception_panic
1:
    cli
    hlt
    jmp 1b
.size nova_exception_common, .-nova_exception_common
.section .note.GNU-stack,"",@progbits
