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

.global nova_exception_stub
.type nova_exception_stub,@function
.extern nova_exception_panic
nova_exception_stub:
    cli
    andq $-16,%rsp
    call nova_exception_panic
1:
    hlt
    jmp 1b
.size nova_exception_stub, .-nova_exception_stub
.section .note.GNU-stack,"",@progbits
