# StrataOS architecture (Phases 1-4)

Boot: QEMU `-kernel` (Multiboot v1) -> `boot.s` sets a 16 KiB stack, calls `kernel_main(magic, mbi)`.

Init order in `kernel_main`: serial + VGA console, GDT (flat ring-0/ring-3 + TSS), PIC remap + IDT (exceptions 0-31, IRQ 0-15 stubs), PIT at 100 Hz, then memory:

1. **PMM (`pmm.c`)**: bitmap frame allocator for the first 256 MiB (`PMM_MAX_PHYS`). The bitmap is a static array in `.bss`, so it can never overwrite free memory. Every frame starts "used". Free regions come from the Multiboot memory map. These are then reserved again: first 1 MiB (BIOS, VGA), the kernel image, the Multiboot info and memory map. A second "owned" bitmap means only frames the allocator handed out can be freed, which rejects double frees and attempts to free kernel frames.
2. **Paging (`paging.c`)**: two-level 32-bit paging with 4 KiB pages. All usable physical memory is identity-mapped, so page tables and the heap's backing frames stay reachable. CR0.WP is set. `paging_map_page`, `paging_unmap_page`, `paging_translate` are the API.
3. **Kernel heap (`kheap.c`)**: first-fit free-list allocator with 16-byte alignment, block splitting, coalescing with both neighbours, and magic numbers for double-free / wild-pointer detection. It lives at virtual `0xD0000000` and grows on demand by asking a callback to map more frames (kernel: `pmm_alloc_frame` + `paging_map_page`). `kheap_check()` walks the block chain and verifies it.

Interrupts: `isr_common` in `boot.s` saves segments, switches data segments to ring 0, saves registers, clears DF and calls `isr_handler` in C, which returns the stack pointer to resume. Kernel page faults print CR2 and exit; user exceptions terminate the task; the timer increments a tick counter and calls the scheduler; the keyboard feeds a ring buffer.

## Phase 2: multitasking (`task.c`, `task.h`)
- **Switch point.** The timer preempts tasks; syscall yield/sleep/exit and user exceptions can also switch tasks. `isr_common` pushes the register frame on the interrupted task's stack, `isr_handler` sends the EOI and returns `schedule(esp)`, and `isr_common` does `mov %eax,%esp; popa; restore segments; add $8,%esp; iret` on whatever stack came back. Switching is just choosing a different saved frame.
- **Task creation.** `task_create` allocates an 8 KiB stack with `kmalloc` and builds a fake interrupt frame on it: 8 zeroed registers, 4 saved kernel segments, `int_no`/`err` words, then `eip`, `cs=0x08`, `eflags=0x202`. Above it sit a return address (`task_exit`) and the argument, so an entry function can simply return. Stack alignment follows the i386 ABI.
- **Scheduler.** Round robin over a fixed table of 16 slots; states READY, SLEEPING, DEAD. The boot context is adopted as task 1 ("main", the shell) and is always runnable, so there is no idle task. `task_sleep` blocks for N ticks and uses `hlt`.
- **Teardown.** A dead task is never freed by itself: `task_reap` (called by the shell before each prompt) frees its stack from another task. `task_kill` just marks the task dead; a task can never be killed in the middle of a heap call because heap calls run with interrupts off.
- **Interrupt safety.** The heap takes optional lock hooks (`kheap_set_lock`); the kernel passes `irq_save`/`irq_restore`, so `kmalloc`/`kfree`/`kheap_check` are atomic with respect to the scheduler, including the PMM and paging calls made while the heap grows. `putc` also disables interrupts so tasks do not garble the console. The `frame`/`unframe` shell commands do the same. Host tests leave the hooks unset.
- **Stack overflow detection.** Two canary words at the bottom of each task stack, checked by the selftest (`task_stack_ok`). There are no guard pages.
- **Limits.** Kernel workers still run in ring 0. No priorities, no blocking primitives besides sleep and no per-task address spaces. Ring-3 support uses these same 8 KiB stacks as TSS entry stacks.

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


## Phase 3: user space (`interrupt.h`, `syscall.c`, `user.s`)

- **GDT/TSS.** User CS=0x1b, data/stack=0x23. A 104-byte 32-bit TSS uses SS0=0x10, descriptor access=0x89, selector=0x28. `schedule` updates ESP0 to the selected task's kernel-stack top. The I/O bitmap offset is beyond the TSS limit, so CPL3 port I/O is denied; user EFLAGS starts with IF=1 and IOPL=0.
- **Interrupt frame.** Assembly saves DS/ES/FS/GS before `pusha`, loads kernel data segments, clears DF, calls C and restores the selected frame's segments before `iret`. The CPU supplies user ESP/SS on privilege changes only. Kernel and user synthetic frames match this layout. Only vector 128 has DPL3 (0xEE interrupt gate); other gates remain DPL0.
- **User memory.** Each slot gets a 64 KiB-spaced arena starting at 0x40000000. Only three pages are used: read-only code, RW stack, and a lazy RW allocation. Frames are zeroed before mapping. All existing identity/kernel/heap PTEs stay supervisor-only; CR0.WP remains set. User code is a position-independent assembly blob copied to its arena, not a kernel C function made executable in user mode.
- **ABI.** EAX=syscall number/result, EBX/ECX=arguments. 0=write(ptr,len), 1=yield, 2=exit, 3=alloc(bytes), 4=ticks, 5=sleep(ticks). Write is capped at 256 bytes and checked against the current task's mapped arena (including overflow); allocation is one zeroed page, requested size 1..4096; sleep is bounded to 10000 ticks. Errors: -1 wrong caller, -12 allocation unavailable, -14 bad user range, -22 bad argument, -38 unknown syscall. Allocation has no free syscall; exit/kill/fault reclamation frees all user frames and the kernel stack. Empty page tables are retained and reused, bounded by fixed arena placement.
- **Faults.** An exception with saved CS.RPL=3 marks the current task DEAD and resumes a runnable task. No user exception invokes kernel panic or the QEMU debug-exit port. The task is freed later from the shell, never from its own interrupt stack. Kernel exceptions retain Phase 1 behavior.
- **Scope.** Shared CR3 means user tasks can access other user arenas directly if they know the address. Syscall pointers are restricted to the caller's arena but this is NOT per-process memory isolation. There is no executable loader, NX support, stack guard page, signals, filesystem, user heap beyond the one-page allocation, or general-purpose userland. This is an educational privilege-transition milestone, not a secure multi-user OS.

### Phase 3 differences from the PDF snippets
The PDF's illustrative code was not copied. A ring-3 C function in supervisor-only kernel text cannot run with the current paging. The sample print syscall dereferences any user-supplied pointer, `popa` discards syscall return values, segment registers are not handled, and `hlt` is not yield. This implementation uses dedicated user mappings, checked ranges, returned EAX, segment-preserving interrupt frames, real scheduling, TSS stack updates and user-fault containment. The PDF's claim that its snippets are already complete is not the repo's status.

### Phase 3 verification
The QEMU harness has 42 assertions across 10 boots. It checks CPL3 and user DS, DF-safe entry, returned errno, supervisor/overflow/out-of-arena/unallocated pointers, oversize writes/allocations, zero-filled writable user allocation and second-allocation failure, sleep/yield/exit, preemption of a never-yielding user loop, and seven protection faults with expected vectors/error bits. Repeated exits at 32 MiB return frames/stacks to baseline (after warming the retained page table). Two user demos and a spinner run together; the existing 51-check kernel selftest runs while the user spinner is preempted. Task-table saturation is refused cleanly and all slots work again after kill/reap. Mac, CI and physical hardware remain unverified.



## Phase 4: disk and filesystem (`ata.c`, `fs.c`)

- **ATA driver.** Polled PIO on the primary bus, master drive, LBA28 (up to 128 GiB; disks are clamped to that). `ata_init` sets nIEN so the drive raises no interrupts, treats a floating bus (status 0xFF) or zero status as "no disk", runs IDENTIFY, rejects ATAPI/SATA signatures and drives without LBA, and records the sector count and model. Every wait is bounded, so a missing or stuck drive returns an error instead of hanging the kernel. Reads and writes return `ATA_ENODEV/ETIMEOUT/EIO/ERANGE`; writes end with a cache flush. Each transfer runs with interrupts off, so the scheduler cannot interleave two users of the controller.
- **StrataFS layout.** LBA 0 superblock (magic `STRATAFS`, version, sizes), LBA 1-8 directory table of 64 entries x 64 bytes, data from LBA 9. An entry has a 31-character name, type (file/dir), parent index, size, and one contiguous extent. Directories are entries whose children point at them as parent; the root is implicit. Limits: 64 entries total, 64 KiB per file, names without spaces or `/`.
- **Safety.** Mounting never writes. It validates the superblock, entry types, names, parent chains, extents inside the disk, duplicate names and overlapping extents; anything inconsistent is refused (`FS_ECORRUPT`) and left as is. A blank disk is "no filesystem" until the user runs `format yes`, which rewrites the directory table and then the superblock. Rewriting a file writes the new data to unused sectors first and updates the single directory sector last, so a failure before that leaves the old content intact. Failed directory writes restore the in-memory entry.
- **Not provided.** No journal (a power cut during the final directory-sector write is not protected beyond the single-sector write), no fragmentation handling (a file needs a contiguous free run, so a fragmented disk can report "no space" with free sectors left), no timestamps or permissions, no caching, no concurrency control beyond the shell being the only caller, and no syscall access for user tasks.
- **Shell.** `diskinfo`, `format yes`, `ls [path]`, `cat`, `write NAME TEXT`, `mkdir`, `rm` (file or empty directory), `cd`, `pwd`, `fsinfo`, plus `mkfile NAME N` and `verify NAME N` for pattern data. Paths may be absolute or relative and support `.` and `..`.
- **Tests.** `fs.c` has no kernel dependencies, so `make test-host` runs it on a RAM disk under ASan/UBSan, including write-failure injection and byte-flip corruption of the directory table. `make test-qemu` boots with disposable images under `build/` (never `disk.img`): no disk, blank disk, format, persistence across reboots, corruption refusal with an unchanged image, disk full.

### Phase 4 differences from the PDF snippets
The PDF's code was not copied. Its driver loops forever on a missing disk or error, ignores error bits, never flushes, and its `ata_init` cannot tell whether a disk exists. Its filesystem formats the disk on the first sight of odd data (including any non-formatted disk), has no size or directory support beyond one 512-byte sector per file, assigns data sectors by directory index and updates the directory before the data, and uses `reboot`, which this kernel does not have.
