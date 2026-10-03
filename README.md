# StrataOS

A small educational 32-bit x86 kernel in C with a little assembly. **Phase 1: memory management** (physical memory manager, paging, kernel heap) and **Phase 2: preemptive multitasking** (kernel threads, round-robin scheduler), plus the minimal boot, console, interrupt and shell code needed to run and test them.

Formerly called miniOS in its planning notes. Status: Phases 1 and 2. All tasks are kernel threads in ring 0; no user mode, syscalls, disk or filesystem yet. Runs in QEMU; not tested on real hardware.

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
- Serial/keyboard shell: `help meminfo uptime selftest frame unframe malloc free heapcheck translate tasks spawn kill crash halt`
- Try it: `spawn blink` (a `t`/`T` blinks in the top-right corner while the shell stays usable), `spawn count 20`, `tasks`, `kill 2`

## Build and run
Linux (or Docker) with `gcc` able to target 32-bit (`gcc-multilib`), `make`, `qemu-system-x86`:

    make            # builds build/kernel.elf
    make run        # boots it in QEMU (-m 128)
    make test-host  # PMM + heap tests under ASan/UBSan
    make test-qemu  # boots in QEMU and drives the shell

On macOS, building needs an i686 cross toolchain (e.g. `i686-elf-gcc`; run `make CC=i686-elf-gcc`). That path is untested here.

## Verified
Built with GCC 11.4 (warnings as errors); 10,142 host checks passed; 24 QEMU 6.2 boot scenarios passed, including a 51-check in-kernel `selftest` (scheduler, preemption of a never-yielding task, and a 3-task + main kmalloc/kfree stress that does fail when the heap lock is removed). CI config is in `.github/workflows/ci.yml` (not yet run).

## Roadmap (planned, not implemented)
These are future phases and do not exist in this repo yet:
- Phase 3: ring 3 user mode, `int 0x80` system calls
- Phase 4: ATA PIO disk driver and a simple filesystem

## License
No license file is included yet.

