/* Copied to a low EfiLoaderCode page before boot services are terminated.
 * SysV arguments: entry, boot_params, CR3, stack top. No firmware references.
 */
.text
.balign 16
.global linux_handoff_start
.global linux_handoff_end
.hidden linux_handoff_start
.hidden linux_handoff_end
linux_handoff_start:
    cli
    cld
    movq %rcx, %rsp
    leaq gdt(%rip), %rax
    movq %rax, gdtr+2(%rip)
    lgdt gdtr(%rip)
    pushq $0x10
    leaq segments(%rip), %rax
    pushq %rax
    lretq
segments:
    movl $0x18, %eax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    xorl %eax, %eax
    movw %ax, %fs
    movw %ax, %gs
    movq %rdx, %cr3
    movq %rdi, %rax
    xorl %ebp, %ebp
    xorl %ebx, %ebx
    xorl %edi, %edi
    jmp *%rax
.balign 8
gdt:
    .quad 0, 0
    .quad 0x00af9a000000ffff
    .quad 0x00cf92000000ffff
gdtr:
    .word 31
    .quad 0
linux_handoff_end:
.section .note.GNU-stack,"",@progbits
