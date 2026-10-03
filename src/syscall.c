#include "syscall.h"
#include "task.h"
#include "kernel.h"

uint32_t syscall_dispatch(struct regs *r) {
    /* Interrupt gate keeps the dispatcher atomic against timer preemption. */
    if ((r->cs & 3) != 3 || !task_is_user()) { r->eax = (uint32_t)-1; return (uint32_t)r; }
    switch (r->eax) {
    case SYS_WRITE:
        if (r->ecx > 256) r->eax = (uint32_t)-22;
        else if (!task_user_range(r->ebx, r->ecx, 0)) r->eax = (uint32_t)-14;
        else { for (uint32_t i = 0; i < r->ecx; i++) putc(((char *)r->ebx)[i]); r->eax = r->ecx; }
        break;
    case SYS_ALLOC: r->eax = task_user_alloc(r->ebx); break;
    case SYS_TICKS: r->eax = timer_ticks(); break;
    case SYS_YIELD: r->eax = 0; return schedule((uint32_t)r);
    case SYS_SLEEP:
        if (r->ebx > 10000) { r->eax = (uint32_t)-22; break; }
        if (r->ebx) task_block(r->ebx);
        r->eax = 0; return schedule((uint32_t)r);
    case SYS_EXIT:
        task_mark_dead(); r->eax = 0; return schedule((uint32_t)r);
    default: r->eax = (uint32_t)-38;
    }
    return (uint32_t)r;
}
