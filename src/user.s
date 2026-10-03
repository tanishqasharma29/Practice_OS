/* Position-independent, one-page built-in user program. No kernel symbols/data. */
.section .rodata.user,"a"
.code32
.global user_blob_start, user_blob_end
user_blob_start:
    call 1f
1:  pop %esi
    sub $(1b-user_blob_start), %esi
    test %edi, %edi
    jz demo
    cmp $8, %edi
    je spin
    cmp $1, %edi
    je bad_kernel
    cmp $2, %edi
    je bad_cli
    cmp $3, %edi
    je bad_io
    cmp $4, %edi
    je bad_code
    cmp $5, %edi
    je bad_interrupt
    cmp $6, %edi
    je bad_null
    mov %esi, %esp              /* stack into read-only code */
    push $0
bad_kernel:
    mov 0xd0000000, %eax        /* supervisor heap */
    jmp failed
bad_cli:
    cli
    jmp failed
bad_io:
    mov $0x3f8, %dx
    outb %al, %dx
    jmp failed
bad_code:
    movb $0, (%esi)
    jmp failed
bad_interrupt:
    int $0x20                  /* IRQ gate has DPL 0 */
    jmp failed
bad_null:
    mov 0, %eax                /* identity map is supervisor-only */
    jmp failed
spin:
    lea (spin_msg-user_blob_start)(%esi), %ebx
    mov $(spin_end-spin_msg), %ecx
    xor %eax, %eax
    int $0x80
2:  jmp 2b                     /* no voluntary syscall or yield */
demo:
    mov %cs, %ax
    and $3, %eax
    cmp $3, %eax
    jne failed
    mov %ds, %ax
    cmp $0x23, %ax
    jne failed
    std                        /* kernel entry must clear DF */
    mov $4, %eax
    int $0x80
    cld
    mov $999, %eax
    int $0x80
    cmp $-38, %eax
    jne failed
    xor %eax, %eax
    mov $0xd0000000, %ebx
    mov $4, %ecx
    int $0x80
    cmp $-14, %eax
    jne failed
    xor %eax, %eax
    mov $0xfffffffe, %ebx       /* overflow */
    mov $8, %ecx
    int $0x80
    cmp $-14, %eax
    jne failed
    xor %eax, %eax
    mov %esi, %ebx
    mov $257, %ecx
    int $0x80
    cmp $-22, %eax
    jne failed
    xor %eax, %eax
    lea 12288(%esi), %ebx       /* outside own arena */
    mov $1, %ecx
    int $0x80
    cmp $-14, %eax
    jne failed
    xor %eax, %eax
    lea 8192(%esi), %ebx        /* not yet allocated */
    mov $1, %ecx
    int $0x80
    cmp $-14, %eax
    jne failed
    mov $3, %eax
    mov $4097, %ebx
    int $0x80
    cmp $-22, %eax
    jne failed
    mov $3, %eax
    xor %ebx, %ebx
    int $0x80
    cmp $-22, %eax
    jne failed
    mov $3, %eax
    mov $32, %ebx
    int $0x80
    lea 8192(%esi), %edx
    cmp %edx, %eax
    jne failed
    mov %eax, %ebp
    cmpl $0, (%ebp)
    jne failed
    movl $0x0a4b4f55, (%ebp)    /* "UOK\n" */
    xor %eax, %eax
    mov %ebp, %ebx
    mov $4, %ecx
    int $0x80
    cmp $4, %eax
    jne failed
    mov $3, %eax
    mov $1, %ebx
    int $0x80
    cmp $-12, %eax
    jne failed
    mov $4, %eax
    int $0x80
    mov %eax, %ebp
    mov $5, %eax
    mov $3, %ebx
    int $0x80
    test %eax, %eax
    jne failed
    mov $4, %eax
    int $0x80
    sub %ebp, %eax
    cmp $3, %eax
    jb failed
    mov $1, %eax
    int $0x80
    test %eax, %eax
    jne failed
    lea (pass_msg-user_blob_start)(%esi), %ebx
    mov $(pass_end-pass_msg), %ecx
    xor %eax, %eax
    int $0x80
    mov $2, %eax
    xor %ebx, %ebx
    int $0x80
failed:
    lea (fail_msg-user_blob_start)(%esi), %ebx
    mov $(fail_end-fail_msg), %ecx
    xor %eax, %eax
    int $0x80
    mov $2, %eax
    mov $1, %ebx
    int $0x80
3:  jmp 3b
pass_msg: .ascii "USER DEMO PASS: ring 3, syscalls, memory, sleep/yield\n"
pass_end:
fail_msg: .ascii "USER DEMO FAIL\n"
fail_end:
spin_msg: .ascii "USER SPIN: ring 3, no voluntary yield\n"
spin_end:
user_blob_end:
.section .note.GNU-stack,"",@progbits
