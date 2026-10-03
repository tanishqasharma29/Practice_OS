# StrataOS

A small educational 32-bit x86 kernel in C with a little assembly. **Phase 1: memory management** (physical memory manager, paging, kernel heap) **Phase 2: preemptive multitasking** (kernel threads, round-robin scheduler) and **Phase 3: user space** (ring 3, TSS, `int 0x80`) and **Phase 4: storage** (ATA PIO disk driver, a small on-disk filesystem with directories), plus the minimal boot, console, interrupt and shell code needed to run and test them.

Formerly called miniOS in its planning notes. Status: Phases 1-4. Kernel threads and built-in ring-3 tasks coexist. Kernel pages are supervisor-only; user tasks share one page directory, so they are not isolated from each other. There is no external program loader, and user programs cannot read files (the filesystem is reached from the kernel shell only). Runs in QEMU; not tested on real hardware.

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
- Phase 4: polled ATA PIO driver for the primary-bus master (IDENTIFY, LBA28, bounded waits, error returns, cache flush after writes) and StrataFS, a small filesystem with directories, 31-character names, files up to 64 KiB (one contiguous extent each) and 64 entries in total. It is not FAT and not a general-purpose filesystem. Metadata is validated on mount; a corrupt or unformatted disk is refused and left untouched, never auto-formatted.
- Serial/keyboard shell: `help meminfo uptime selftest frame unframe malloc free heapcheck translate tasks spawn kill user crash halt diskinfo format ls cat write mkdir rm cd pwd fsinfo mkfile verify`
- Try it: `spawn blink` (a `t`/`T` blinks in the top-right corner while the shell stays usable), `spawn count 20`, `tasks`, `kill 2`

Try Phase 3:

    user demo      # checks CPL=3, syscall errors, allocation, sleep/yield; prints USER DEMO PASS
    user spin      # never yields; shell remains responsive; use tasks and kill ID
    user fault 1   # touch supervisor heap; task faults, shell survives

Try Phase 4 (`make run` creates a 10 MB `disk.img` only if it does not exist and never overwrites it):

    diskinfo                         # model and size of the ATA disk
    format yes                       # first time only: creates StrataFS (erases the file table on this disk)
    write hello.txt Hello there
    mkdir docs
    write docs/note.txt inside docs
    ls                               # lists docs/ and hello.txt
    cat docs/note.txt
    cd docs                          # also: pwd, cd .., ls PATH, rm PATH (file or empty dir), fsinfo
    mkfile big 65536                 # N bytes of test data; verify big 65536 checks it

Type `halt`, run `make run` again, and `cat hello.txt` still works. `write` takes the rest of the line (up to 127 characters).

Fault probes 1-7: supervisor heap read, CLI, port I/O, code write, privileged IRQ interrupt, null read, bad user stack. These are deliberate task termination tests.

## Build and run
Linux (or Docker) with `gcc` able to target 32-bit (`gcc-multilib`), `make`, `qemu-system-x86`:

    make            # builds build/kernel.elf
    make run        # boots it in QEMU (-m 128) with disk.img attached (created if missing)
    make test-host  # PMM + heap + filesystem tests under ASan/UBSan
    make test-qemu  # boots in QEMU and drives the shell

On macOS, building needs an i686 cross toolchain (e.g. `i686-elf-gcc`; run `make CC=i686-elf-gcc`). That path is untested here.

## Verified
Built with GCC 11.4 (warnings as errors). 11,596 host checks passed under ASan/UBSan (the earlier 10,142 plus filesystem tests with write-failure injection and corrupted metadata). 61 QEMU 6.2 assertions passed across 15 boots, including a 51-check in-kernel `selftest` (scheduler, preemption, heap stress), the Phase 3 ring-3 tests, and Phase 4 tests on disposable disk images: no disk, blank disk not auto-formatted, format/write/read, directories, 64 KiB files, persistence across reboots, a corrupt image refused and left byte-identical, disk full. The disk driver was tested only on QEMU's emulated IDE disk, not real hardware. CI config is in `.github/workflows/ci.yml` (not yet run).

## Roadmap (planned, not implemented)
No further phases are planned in this repo. Ideas not done: an executable loader, per-process address spaces, a block cache, FAT compatibility.

## License
No license file is included yet.
