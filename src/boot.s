/* Multiboot v1 entry, interrupt stubs. 32-bit x86, AT&T syntax. */
.set MB_MAGIC, 0x1BADB002
.set MB_FLAGS, 0x00000003            /* page-align modules + memory info */
.set MB_CHECK, -(MB_MAGIC + MB_FLAGS)

.section .multiboot,"a"
.align 4
.long MB_MAGIC
.long MB_FLAGS
.long MB_CHECK

.section .bss
.align 16
stack_bottom:
.skip 16384
stack_top:

.section .text
.global _start
_start:
    cli
    mov $stack_top, %esp
    push %ebx            /* multiboot info pointer */
    push %eax            /* multiboot magic */
    call kernel_main
1:  cli
    hlt
    jmp 1b

.macro ISR_NOERR n
isr\n:
    push $0
    push $\n
    jmp isr_common
.endm
.macro ISR_ERR n
isr\n:
    push $\n
    jmp isr_common
.endm

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31
.irp n,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47
ISR_NOERR \n
.endr

isr_common:
    pusha
    cld
    push %esp
    call isr_handler       /* returns the stack pointer to resume (changes on a task switch) */
    mov %eax, %esp
    popa
    add $8, %esp
    iret

.section .data
.global isr_stub_table
isr_stub_table:
.irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47
.long isr\n
.endr
.section .note.GNU-stack,"",@progbits

