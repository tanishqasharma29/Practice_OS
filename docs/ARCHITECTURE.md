# StrataOS architecture (Phases 1-2)

Boot: QEMU `-kernel` (Multiboot v1) -> `boot.s` sets a 16 KiB stack, calls `kernel_main(magic, mbi)`.

Init order in `kernel_main`: serial + VGA console, GDT (flat ring-0), PIC remap + IDT (exceptions 0-31, IRQ 0-15 stubs), PIT at 100 Hz, then memory:

1. **PMM (`pmm.c`)**: bitmap frame allocator for the first 256 MiB (`PMM_MAX_PHYS`). The bitmap is a static array in `.bss`, so it can never overwrite free memory. Every frame starts "used". Free regions come from the Multiboot memory map. These are then reserved again: first 1 MiB (BIOS, VGA), the kernel image, the Multiboot info and memory map. A second "owned" bitmap means only frames the allocator handed out can be freed, which rejects double frees and attempts to free kernel frames.
2. **Paging (`paging.c`)**: two-level 32-bit paging with 4 KiB pages. All usable physical memory is identity-mapped, so page tables and the heap's backing frames stay reachable. CR0.WP is set. `paging_map_page`, `paging_unmap_page`, `paging_translate` are the API.
3. **Kernel heap (`kheap.c`)**: first-fit free-list allocator with 16-byte alignment, block splitting, coalescing with both neighbours, and magic numbers for double-free / wild-pointer detection. It lives at virtual `0xD0000000` and grows on demand by asking a callback to map more frames (kernel: `pmm_alloc_frame` + `paging_map_page`). `kheap_check()` walks the block chain and verifies it.

Interrupts: `isr_common` in `boot.s` pushes registers and calls `isr_handler` in C, which returns the stack pointer to resume. Page faults print CR2 and exit; the timer increments a tick counter and calls the scheduler; the keyboard feeds a ring buffer.

## Phase 2: multitasking (`task.c`, `task.h`)
- **Switch point.** Only the timer interrupt switches tasks. `isr_common` pushes the register frame on the interrupted task's stack, `isr_handler` sends the EOI and returns `schedule(esp)`, and `isr_common` does `mov %eax,%esp; popa; add $8,%esp; iret` on whatever stack came back. Switching is just choosing a different saved frame.
- **Task creation.** `task_create` allocates an 8 KiB stack with `kmalloc` and builds a fake interrupt frame on it: 8 zeroed registers, `int_no`/`err` words, then `eip`, `cs=0x08`, `eflags=0x202`. Above it sit a return address (`task_exit`) and the argument, so an entry function can simply return. Stack alignment follows the i386 ABI.
- **Scheduler.** Round robin over a fixed table of 16 slots; states READY, SLEEPING, DEAD. The boot context is adopted as task 1 ("main", the shell) and is always runnable, so there is no idle task. `task_sleep` blocks for N ticks and uses `hlt`.
- **Teardown.** A dead task is never freed by itself: `task_reap` (called by the shell before each prompt) frees its stack from another task. `task_kill` just marks the task dead; a task can never be killed in the middle of a heap call because heap calls run with interrupts off.
- **Interrupt safety.** The heap takes optional lock hooks (`kheap_set_lock`); the kernel passes `irq_save`/`irq_restore`, so `kmalloc`/`kfree`/`kheap_check` are atomic with respect to the scheduler, including the PMM and paging calls made while the heap grows. `putc` also disables interrupts so tasks do not garble the console. The `frame`/`unframe` shell commands do the same. Host tests leave the hooks unset.
- **Stack overflow detection.** Two canary words at the bottom of each task stack, checked by the selftest (`task_stack_ok`). There are no guard pages.
- **Limits.** Everything runs in ring 0 on its own kernel stack; no priorities, no blocking primitives besides sleep, no per-task address spaces. Phase 3 (ring 3) will need a TSS with `esp0` and a per-task kernel stack, which this layout leaves room for.

## How Phase 2 differs from the AI-generated plan
The plan's code (a separate `irq0_stub`, `schedule` in a `task.c` using `cli`/`sti` in `create_task`) was not copied:
- The plan assumes a dedicated IRQ0 stub; this repo routes everything through `isr_common`, so the fake task frame here includes the `int_no`/`err` words the plan's frame lacks (without them the first switch would misalign the stack).
- The plan's `create_task` ends with an unconditional `sti`; this saves and restores the interrupt flag.
- The plan has no heap locking, so a switch in the middle of `kmalloc` would corrupt the heap; here the heap is protected and the selftest has a stress test that fails without the lock.
- The plan has no task exit or cleanup; a task whose function returns would jump to garbage. Here return goes to `task_exit`, and stacks are reaped.
- The plan's demo task busy-waits; here demo tasks sleep.

## Differences from the AI-generated plan this was based on
The plan's code was not copied. Issues found and handled differently here:
- Plan puts the PMM bitmap right after the kernel in free memory; this uses a static bitmap and reserves regions explicitly.
- Plan's heap init allocates 256 frames one by one and assumes they are contiguous; this maps frames into a contiguous virtual range, which does not need contiguity.
- Plan's paging maps 4096 pages (16 MiB) while its comment says 4 MiB; this maps all usable RAM up to 256 MiB.
- Plan's `pmm_free_frame` frees any set bit, including kernel frames; this tracks ownership.
- Plan has no tests; see below.

## Tests
- `make test-host`: PMM and heap compiled for the host with ASan/UBSan, including a 10,000-operation randomized stress test with content verification.
- `make test-qemu`: boots the kernel, drives the shell over serial, runs the in-kernel `selftest` (51 checks), checks heap growth, page fault handling, exit codes, `spawn`/`tasks`/`kill`, and that the selftest passes with other tasks already running.
- Scheduler selftest highlights: a task that returns is reaped; a never-yielding task is preempted while main also busy-waits; three tasks and main hammer `kmalloc`/`kfree` for ~40,000 rounds each with content checks; the task table fills to exactly 16 and then refuses; all stacks go back to the heap.

