# StrataOS

![boot screenshot](docs/boot-screenshot.png)

**StrataOS** is an educational 32-bit x86 kernel built in C and Assembly. Currently in **Phase 1 (Memory Management)**, the project features a fully functional physical memory manager, virtual memory paging, and a custom kernel heap (`kmalloc`/`kfree`), alongside the foundational boot, console, interrupt, and shell infrastructure required to run and test the memory subsystems.

*Note: This project is currently in Phase 1. Processes, process scheduling, user mode, and filesystems are not yet implemented. It is designed to run in QEMU and has not been tested on bare metal.*

## Features

**System Foundations**
*   Multiboot-compliant bootloader integration.
*   Flat Global Descriptor Table (GDT) and Interrupt Descriptor Table (IDT).
*   Programmable Interrupt Controller (PIC) and 100 Hz PIT timer.
*   Hardware interfacing: PS/2 keyboard support and VGA text + COM1 serial console.

**Memory Management (Phase 1)**
*   **Physical Memory Manager (PMM):** Bitmap-based allocator driven by the Multiboot memory map, featuring robust double-free protection.
*   **Paging (Virtual Memory):** 4 KiB-page paging subsystem supporting map, unmap, and translate operations, coupled with a page-fault handler.
*   **Dynamic Heap:** Growable first-fit kernel heap allocator supporting block coalescing, strict alignment, and memory corruption checks.

**Interactive Shell**
*   Serial and keyboard-driven interactive shell for live testing.
*   **Supported Commands:** `help`, `meminfo`, `uptime`, `selftest`, `frame`, `unframe`, `malloc`, `free`, `heapcheck`, `translate`, `crash`, `halt`.

## Getting Started

### Prerequisites
To build and run StrataOS, you will need a Linux environment (or Docker) equipped with:
*   `gcc` capable of targeting 32-bit architecture (`gcc-multilib`)
*   `make`
*   `qemu-system-x86`

*Note for macOS users: Building requires an i686 cross-toolchain (e.g., `i686-elf-gcc`). You can compile using `make CC=i686-elf-gcc`, though this path is currently untested.*

### Build and Run Instructions

```bash
# Build the kernel (compiles to build/kernel.elf)
make

# Boot the kernel in QEMU with 128MB of RAM
make run

# Execute PMM and heap tests under ASan/UBSan on the host
make test-host

# Boot in QEMU and automatically drive the interactive shell
make test-qemu
```

## Testing & Verification

StrataOS prioritizes stability and correctness through strict compilation flags and automated testing:
*   Compiled utilizing **GCC 11.4** with warnings treated as errors.
*   Successfully passes **10,142 host checks**.
*   Successfully passes **14 distinct QEMU 6.2 boot scenarios** (located in `tests/`).
*   Continuous Integration (CI) configuration is prepared in `.github/workflows/ci.yml`.

## Development Context & Architecture

This project was initially planned under the working title *miniOS*. It was structured and prepared with AI assistance (leveraging a systems engineering memory-subsystem plan). 

To maximize educational value, the codebase was written and tested fresh from scratch rather than directly copying generated chat code. Any design divergences between the initial AI roadmap and the final implementation are documented in [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md). 

## Roadmap (Future Implementations)

The following phases outline the planned expansion of the kernel architecture (not yet implemented in this repository):
*   **Phase 2:** Process control blocks (PCBs), context switching, and a preemptive round-robin scheduler.
*   **Phase 3:** Ring 3 unprivileged user-mode execution and `int 0x80` system call interface.
*   **Phase 4:** ATA PIO disk device driver and a simple persistent filesystem.

## License

*(No license file is included yet.)*
