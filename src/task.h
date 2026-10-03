#ifndef TASK_H
#define TASK_H
#include <stdint.h>

#define MAX_TASKS   16
#define TASK_STACK  8192u
#define TASK_NAME   12

enum task_state { TASK_UNUSED = 0, TASK_READY, TASK_SLEEPING, TASK_DEAD };

struct task_info {              /* snapshot for the "tasks" command */
    uint32_t id; int state; int is_current; uint32_t switches; char name[TASK_NAME];
};

void     tasking_init(void);                                    /* adopt the boot context as task 1 ("main") */
int      task_create(const char *name, void (*fn)(void *), void *arg);   /* returns task id, or -1 */
void     task_exit(void);                                       /* end the calling task (also runs if fn returns) */
int      task_kill(uint32_t id);                                /* 0 ok, -1 unknown/main/self */
void     task_sleep(uint32_t ticks);                            /* block the calling task for N timer ticks (100 Hz) */
int      task_reap(void);                                       /* free stacks of finished tasks; returns count freed */
int      task_count(void);                                      /* live (non-unused, non-dead) tasks */
int      task_list(struct task_info *out, int max);
uint32_t task_current_id(void);
int      task_stack_ok(void);                                   /* 1 if no stack canary is damaged */

/* Called from the timer interrupt with the interrupted context's stack pointer;
   returns the stack pointer to resume (equal to the argument when no switch happens). */
uint32_t schedule(uint32_t esp);
#endif
