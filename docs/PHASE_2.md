# Phase 2: preemptive multitasking

Phase 2 adds round-robin kernel threads on top of Phase 1 memory management. Timer interrupts can switch tasks even when a worker does not yield. The shell remains a task in the same scheduler.

Prepared with AI assistance. This documentation describes the published implementation. Source reviewed on 4 October 2026 at commit `db0bc453198fcad0890823d0dc4c46e5fa81fd28`.

## Task model

`src/task.c` and `src/task.h` hold a fixed table of 16 slots, including the boot context adopted as task 1, named `main`. New workers receive an 8 KiB heap-allocated stack and a task ID. IDs and table slots are different: a reused slot does not mean the old ID is reused.

States are UNUSED, READY, SLEEPING and DEAD. Creation fails when the table is full or memory is unavailable. Task names fit in a 12-byte field including the terminator.

A new kernel stack contains a synthetic interrupt frame matching the assembly restore path. Its entry function receives an argument; returning from that function goes to `task_exit` rather than an undefined return address.

## Context switching

The PIT runs at 100 Hz. The shared interrupt entry in `src/boot.s` saves the interrupted context, loads kernel data segments and clears the direction flag before calling the C handler.

The scheduler saves the current stack pointer, wakes sleeping tasks whose deadline has arrived, and scans the table round-robin for a READY task. The handler returns the chosen saved stack pointer. Assembly restores that frame and uses `iret` to resume execution.

The current build also allows scheduling from ring-3 yield, sleep, exit and fault paths introduced in Phase 3. The same task table supports both kernel and user tasks; this does not create separate process address spaces.

## Sleep, exit, kill and cleanup

- `task_sleep(ticks)` marks a kernel task sleeping until its timer deadline. One tick is nominally 10 ms; scheduling is not a real-time guarantee.
- `task_exit()` marks the calling task DEAD and waits for a timer switch away.
- `task_kill(id)` marks another READY or SLEEPING worker DEAD. Killing main, the caller or an unknown task is refused.
- `task_reap()` frees dead workers from another task's context. The shell calls it before each prompt, so reclamation is deferred rather than performed on the dying stack.

In the current Phase 3-enabled build, reaping also releases a user's arena frames. Empty paging tables remain available for reuse.

## Interrupt safety and stack checks

The kernel installs interrupt-save/restore hooks around heap operations. This protects allocation, free and heap growth from preemption while their metadata is being changed. Console character output and the shell's frame-allocation commands also use interrupt protection.

Each allocated task stack has two canary words at its bottom. The selftest checks them through `task_stack_ok`. Canaries detect damage after it occurs; they are not guard pages and cannot prevent a stack overflow.

## Task lifecycle in one view

```text
UNUSED -> READY -> SLEEPING -> READY
             |                  |
             +------> DEAD <----+
                        |
                 reap from another task
                        |
                     UNUSED
```

The transition to DEAD stops future scheduling of that worker. It does not immediately release the stack. Reaping is separate because freeing the stack that is currently executing would invalidate the running context. Returning from a kernel entry function follows the same exit path as an explicit `task_exit`.

Main occupies one table slot, so at most 15 additional live tasks fit simultaneously. Dead slots become available after reaping. A newly created worker may therefore fail even though the shell is responsive; check the task list and allow the next prompt to perform cleanup.

## Core API reference

| API | Purpose |
| --- | --- |
| `tasking_init()` | Adopt the boot context as main |
| `task_create(name, fn, arg)` | Build a stack/frame and return an ID or -1 |
| `schedule(esp)` | Return the saved frame to resume |
| `task_sleep(ticks)` | Block a kernel worker until its wake tick |
| `task_kill(id)` | Mark another running worker dead; return 0 or -1 |
| `task_reap()` | Free eligible dead workers; return count reclaimed |
| `task_list(out,max)` | Snapshot IDs, names, state and switch counts |
| `task_stack_ok()` | Check canaries on allocated task stacks |

## Interpreting the demonstrations

The blink demo sleeps between changes, allowing other tasks to run. The count demo also sleeps; it is not intended to monopolize the CPU. The churn demo repeatedly allocates buffers, writes/checks content and frees them. It helps exercise allocator behavior while scheduling is active.

`tasks` switch counts are diagnostic counts, not CPU time or a fairness guarantee. The PIT frequency controls nominal scheduling opportunities, but interrupt-disabled sections can delay them. In the current storage-enabled build, ATA polling transfers also disable interrupts, so this scheduler is not suitable for strict latency guarantees.

If creation prints `could not create task`, the reason is table capacity or allocation failure. Use `tasks`, stop only the intended worker by its printed ID, then try again after a prompt. A kernel worker fault is not isolated like a Phase 3 user fault: ring-0 workers share the kernel's privileges and mappings.

## Try it in QEMU

Run `make` and `make run`, then enter:

```text
spawn blink
spawn count 20
tasks
heapcheck
selftest
```

`spawn blink` changes a character in the upper-right corner. `spawn count 20` runs a counting demo. `spawn churn 100` runs an allocation/free demo. Use `tasks` to see IDs, states and switch counts. To stop a worker, enter `kill ID`, replacing `ID` with the actual worker ID from that list. Do not assume a fixed ID after other tasks have run.

## Verification

The QEMU harness checks task creation, listing, kill refusal, shell responsiveness and selftest execution while workers run. Scheduler selftests cover returning tasks, preemption of a non-yielding worker, concurrent heap stress, table saturation, stack canaries and reclamation.

The previously reported published-build result was 11,596 host checks and 61 QEMU assertions across Phases 1-4. These are cumulative totals, not separate Phase 2 test counts. No tests were rerun for this documentation-only change. The workflow exists in the repository, but GitHub Actions execution, macOS and physical hardware remain unverified.

## Limits

Kernel workers execute in ring 0 and can damage the kernel. There are no priorities, per-task address spaces, mutexes, condition variables or stack guard pages. Sleep is the only general blocking primitive. The shell supplies the always-runnable main context; no separate idle task is implemented. This is a small educational scheduler, not a fault-isolated process system.

## Source and related documentation

Primary files: `src/task.c`, `src/task.h`, `src/demo_tasks.c`, `src/boot.s`, `src/kernel.c`, `src/selftest.c` and `tests/qemu_test.py`.

[Architecture](ARCHITECTURE.md) | [Phase 1](PHASE_1.md) | [Phase 3](PHASE_3.md) | [Phase 4](PHASE_4.md) | [Project README](../README.md)
