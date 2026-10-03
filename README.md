# StrataOS

A small educational 32-bit x86 kernel in C with a little assembly. **Phase 1: memory management** (physical memory manager, paging, kernel heap) **Phase 2: preemptive multitasking** (kernel threads, round-robin scheduler) and **Phase 3: user space** (ring 3, TSS, `int 0x80`), plus the minimal boot, console, interrupt and shell code needed to run and test them.

Formerly called miniOS in its planning notes. Status: Phases 1-3. Kernel threads and built-in ring-3 tasks coexist. Kernel pages are supervisor-only; user tasks share one page directory, so they are not isolated from each other. No disk, filesystem or external program loader yet. Runs in QEMU; not tested on real hardware.

**AI assistance:** this project was prepared with AI help (following a kernel expansion plan from a Gemini chat). The code was written and tested fresh rather than copied from that chat, and the design differences are listed in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md). It is not a from-scratch solo effort, and it was not built over two months.

![boot screenshot](docs/boot-screenshot.png)

Phase 2 (blinking `t` top right is a task; selftest passing while tasks run):

![phase 2 screenshot](docs/phase2-screenshot.png)

## Features
- Multiboot boot, flat GDT, IDT, PIC, 100 Hz PIT timer, PS/2 keyboard, VGA text + COM1 serial console
- Bitmap PMM driven by the Multiboot memory map, with double-free protection
- 4 KiB-page paging with map/unmap/translate, page-fault handler
- Growable first-fit heap with coalescing, alignment and corruption checks
- Preemptive round-robin kernel threads: a task table of 16, 8 KiB heap-allocated stacks with a canary, context switch inside the timer interrupt, `task_sleep`, kill, and stack reaping
- Heap and console are interrupt-safe, so tasks can `kmalloc`/`kfree` and print concurrently
- Ring-3 code/data segments and 32-bit TSS with per-task kernel entry stacks; interrupt entry saves/restores segments and clears DF
- `int 0x80`: bounded console write, yield, exit, one zeroed 4 KiB user allocation, ticks and sleep; invalid pointers/syscalls return negative error codes
- Read-only user code + RW stack; supervisor-only kernel/identity/heap pages; user faults stop the offending task, not the kernel
- Built-in demo only: no ELF loader, per-process CR3, user `free`, guard pages or full security isolation
- Serial/keyboard shell: `help meminfo uptime selftest frame unframe malloc free heapcheck translate tasks spawn kill user crash halt`
- Try it: `spawn blink` (a `t`/`T` blinks in the top-right corner while the shell stays usable), `spawn count 20`, `tasks`, `kill 2`

Try Phase 3:

    user demo      # checks CPL=3, syscall errors, allocation, sleep/yield; prints USER DEMO PASS
    user spin      # never yields; shell remains responsive; use tasks and kill ID
    user fault 1   # touch supervisor heap; task faults, shell survives

Fault probes 1-7: supervisor heap read, CLI, port I/O, code write, privileged IRQ interrupt, null read, bad user stack. These are deliberate task termination tests.

## Build and run
Linux (or Docker) with `gcc` able to target 32-bit (`gcc-multilib`), `make`, `qemu-system-x86`:

    make            # builds build/kernel.elf
    make run        # boots it in QEMU (-m 128)
    make test-host  # PMM + heap tests under ASan/UBSan
    make test-qemu  # boots in QEMU and drives the shell

On macOS, building needs an i686 cross toolchain (e.g. `i686-elf-gcc`; run `make CC=i686-elf-gcc`). That path is untested here.

## Verified
Built with GCC 11.4 (warnings as errors); 10,142 host checks passed; 42 QEMU 6.2 assertions passed across 10 boot scenarios, including a 51-check in-kernel `selftest` (scheduler, preemption of a never-yielding task, and a 3-task + main kmalloc/kfree stress that does fail when the heap lock is removed). Phase 3 tests execute actual ring-3 instructions, seven fault probes, repeated 32 MB allocation/exit/reaping and simultaneous user arenas, plus table-capacity refusal and kill/reuse. CI config is in `.github/workflows/ci.yml` (not yet run).

## Roadmap (planned, not implemented)
These are future phases and do not exist in this repo yet:
- Phase 4: ATA PIO disk driver and a simple filesystem

## License
No license file is included yet.
