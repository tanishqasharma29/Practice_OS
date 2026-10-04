# StrataOS Phases 1-3 summary

StrataOS is a small educational 32-bit x86 kernel in C with a little assembly. It runs in QEMU. This page summarizes what the published Phases 1-3 contain, how they were tested, and what they do not do. It was prepared with AI assistance (see the README); the code was written and tested fresh, not copied from the Gemini plan it followed.

## Phase 1: memory management
- Bitmap physical memory manager for the first 256 MiB, driven by the Multiboot memory map, with double-free and kernel-frame-free protection.
- Two-level 4 KiB-page paging with map, unmap and translate, plus a page-fault handler.
- Growable first-fit kernel heap at 0xD0000000 with alignment, coalescing and corruption checks.
- Supporting code: Multiboot boot, GDT, IDT, PIC, 100 Hz PIT timer, PS/2 keyboard, VGA and COM1 serial console, a small shell.

## Phase 2: preemptive multitasking
- Round-robin kernel threads, task table of 16, 8 KiB heap-allocated stacks with a canary.
- Context switch inside the timer interrupt; sleep, kill and stack reaping.
- Heap and console are interrupt-safe so tasks can allocate and print concurrently.

## Phase 3: user space
- Ring-3 code/data segments and a 32-bit TSS with per-task kernel entry stacks.
- `int 0x80` syscalls: bounded console write, yield, exit, one zeroed 4 KiB user allocation, ticks and sleep. Invalid pointers or syscalls return negative error codes.
- Read-only user code, RW user stack, supervisor-only kernel pages. A faulting user task is stopped; the kernel survives.

## Testing
- `make test-host`: PMM and heap tests under ASan/UBSan, 10,142 checks passed.
- `make test-qemu`: 42 assertions across 10 QEMU 6.2 boot scenarios passed, including a 51-check in-kernel selftest and Phase 3 ring-3 execution, seven fault probes and allocation/exit/reaping stress.
- Built with GCC 11.4, warnings as errors.

## Not verified
- Real hardware.
- macOS build path (needs an i686 cross toolchain; untested).
- The GitHub Actions workflow is in the repo but has not been confirmed run.

## Known limits
- User tasks share one page directory, so they are not isolated from each other.
- Ring-3 support is a built-in demo only: no ELF or external program loader, no per-process CR3, no user `free`, no guard pages, no full security isolation.
- At the Phases 1-3 milestone, there was no disk driver or filesystem. Phase 4 is now published; see [Phase 4 documentation](PHASE_4.md).
- No license file is included.
