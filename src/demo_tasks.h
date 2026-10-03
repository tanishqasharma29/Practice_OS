#ifndef DEMO_TASKS_H
#define DEMO_TASKS_H
#include <stdint.h>
/* Worker bodies used by the shell's "spawn" command and by the selftest. */
void demo_blink(void *arg);        /* toggles a char in the top-right VGA cell until killed */
void demo_count(void *arg);        /* arg = N: sleeps 10 ticks, bumps demo_counter, N times, then exits */
struct count_job { uint32_t n; volatile uint32_t done; };
void demo_count_job(void *arg);    /* arg = struct count_job *: like demo_count but reports into job->done */
void demo_churn(void *arg);        /* arg = rounds: kmalloc/fill/verify/kfree loop; mismatches add to demo_errors */
void demo_spin(void *arg);         /* busy-loops forever (never yields): proves preemption */
extern volatile uint32_t demo_spin_count;
extern volatile uint32_t demo_counter, demo_errors, demo_churn_done;
#endif
