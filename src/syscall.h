#ifndef SYSCALL_H
#define SYSCALL_H
#include "interrupt.h"
/* int 0x80: eax=number/result, ebx/ecx=arguments. Negative errno on failure. */
#define SYS_WRITE 0
#define SYS_YIELD 1
#define SYS_EXIT  2
#define SYS_ALLOC 3
#define SYS_TICKS 4
#define SYS_SLEEP 5
uint32_t syscall_dispatch(struct regs *r);
#endif
