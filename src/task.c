/* Preemptive round-robin kernel threads. All tasks run in ring 0 and switch inside the timer
   interrupt: isr_common saves the registers on the interrupted stack, schedule() picks the next
   task, and isr_common resumes on that task's stack. */
#include "task.h"
#include "kernel.h"
#include "kheap.h"

#define CANARY 0x57A7A05Du

struct task {
    uint32_t id;
    int state;
    uint32_t esp;            /* saved stack pointer (points at the saved register frame) */
    uint32_t *stack;         /* kmalloc'd stack, NULL for the boot task */
    uint32_t wake_tick;
    uint32_t switches;
    char name[TASK_NAME];
};

static struct task tasks[MAX_TASKS];
static struct task *current;
static uint32_t next_id = 1;

static void copy_name(char *d, const char *s) {
    int i = 0;
    for (; s && s[i] && i < TASK_NAME - 1; i++) d[i] = s[i];
    d[i] = 0;
}

void tasking_init(void) {
    uint32_t f = irq_save();
    memset(tasks, 0, sizeof tasks);
    tasks[0].id = next_id++; tasks[0].state = TASK_READY; copy_name(tasks[0].name, "main");
    current = &tasks[0];
    irq_restore(f);
}

void task_exit(void) {
    uint32_t f = irq_save();
    current->state = TASK_DEAD;
    irq_restore(f | 0x200);                    /* the timer must be able to switch away from us */
    for (;;) __asm__ volatile("hlt");
}

/* A fresh task's first run happens through isr_common's "popa; add $8,%esp; iret", so its stack
   is built to look like an interrupt frame (with the int_no/err words), followed by the C
   calling-convention slots seen after iret: return address (task_exit) and the argument. */
int task_create(const char *name, void (*fn)(void *), void *arg) {
    uint32_t *stack = kmalloc(TASK_STACK);
    if (!stack) return -1;
    uint32_t f = irq_save();
    struct task *t = NULL;
    for (int i = 0; i < MAX_TASKS; i++) if (tasks[i].state == TASK_UNUSED) { t = &tasks[i]; break; }
    if (!t) { irq_restore(f); kfree(stack); return -1; }
    stack[0] = CANARY; stack[1] = CANARY;
    uint32_t *sp = (uint32_t *)((uint8_t *)stack + TASK_STACK - 4);   /* keeps esp % 16 == 12 at fn entry */
    *--sp = (uint32_t)arg;
    *--sp = (uint32_t)task_exit;             /* fn returns here */
    *--sp = 0x202;                           /* eflags: IF set */
    *--sp = 0x08;                            /* cs */
    *--sp = (uint32_t)fn;                    /* eip */
    *--sp = 0; *--sp = 0;                    /* err, int_no */
    for (int i = 0; i < 8; i++) *--sp = 0;   /* eax ecx edx ebx esp_unused ebp esi edi */
    t->id = next_id++; t->stack = stack; t->esp = (uint32_t)sp; t->wake_tick = 0; t->switches = 0;
    copy_name(t->name, name);
    t->state = TASK_READY;                   /* last: only now can the scheduler see it */
    int id = (int)t->id;
    irq_restore(f);
    return id;
}

int task_kill(uint32_t id) {
    int rc = -1;
    uint32_t f = irq_save();
    for (int i = 1; i < MAX_TASKS; i++)
        if (tasks[i].id == id && (tasks[i].state == TASK_READY || tasks[i].state == TASK_SLEEPING) && &tasks[i] != current) {
            tasks[i].state = TASK_DEAD; rc = 0;
        }
    irq_restore(f);
    return rc;
}

void task_sleep(uint32_t ticks) {
    uint32_t f = irq_save();
    current->wake_tick = timer_ticks() + ticks;
    current->state = TASK_SLEEPING;
    irq_restore(f | 0x200);
    while (current->state == TASK_SLEEPING) __asm__ volatile("hlt");
}

int task_reap(void) {
    int n = 0;
    for (int i = 1; i < MAX_TASKS; i++) {
        uint32_t *stack = NULL;
        uint32_t f = irq_save();
        if (tasks[i].state == TASK_DEAD && &tasks[i] != current) {
            stack = tasks[i].stack;
            memset(&tasks[i], 0, sizeof tasks[i]);       /* slot is free again */
        }
        irq_restore(f);
        if (stack) { kfree(stack); n++; }
    }
    return n;
}

int task_count(void) {
    int n = 0;
    uint32_t f = irq_save();
    for (int i = 0; i < MAX_TASKS; i++) if (tasks[i].state == TASK_READY || tasks[i].state == TASK_SLEEPING) n++;
    irq_restore(f);
    return n;
}

int task_list(struct task_info *out, int max) {
    int n = 0;
    uint32_t f = irq_save();
    for (int i = 0; i < MAX_TASKS && n < max; i++) {
        if (tasks[i].state == TASK_UNUSED) continue;
        out[n].id = tasks[i].id; out[n].state = tasks[i].state; out[n].is_current = (&tasks[i] == current);
        out[n].switches = tasks[i].switches; copy_name(out[n].name, tasks[i].name);
        n++;
    }
    irq_restore(f);
    return n;
}

uint32_t task_current_id(void) { return current ? current->id : 0; }

int task_stack_ok(void) {
    int ok = 1;
    uint32_t f = irq_save();
    for (int i = 1; i < MAX_TASKS; i++)
        if (tasks[i].stack && (tasks[i].stack[0] != CANARY || tasks[i].stack[1] != CANARY)) ok = 0;
    irq_restore(f);
    return ok;
}

uint32_t schedule(uint32_t esp) {
    if (!current) return esp;
    current->esp = esp;
    uint32_t now = timer_ticks();
    for (int i = 0; i < MAX_TASKS; i++)
        if (tasks[i].state == TASK_SLEEPING && (int32_t)(now - tasks[i].wake_tick) >= 0) tasks[i].state = TASK_READY;
    int start = (int)(current - tasks);
    for (int k = 1; k <= MAX_TASKS; k++) {
        struct task *t = &tasks[(start + k) % MAX_TASKS];
        if (t->state == TASK_READY) {
            if (t != current) { t->switches++; current = t; }
            return current->esp;
        }
    }
    return esp;     /* nothing else runnable: keep running the interrupted task */
}
