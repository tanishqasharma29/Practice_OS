# StrataOS Phase 1 architecture

Boot: QEMU `-kernel` (Multiboot v1) -> `boot.s` sets a 16 KiB stack, calls `kernel_main(magic, mbi)`.

Init order in `kernel_main`: serial + VGA console, GDT (flat ring-0), PIC remap + IDT (exceptions 0-31, IRQ 0-15 stubs), PIT at 100 Hz, then memory:

1. **PMM (`pmm.c`)**: bitmap frame allocator for the first 256 MiB (`PMM_MAX_PHYS`). The bitmap is a static array in `.bss`, so it can never overwrite free memory. Every frame starts "used". Free regions come from the Multiboot memory map. These are then reserved again: first 1 MiB (BIOS, VGA), the kernel image, the Multiboot info and memory map. A second "owned" bitmap means only frames the allocator handed out can be freed, which rejects double frees and attempts to free kernel frames.
2. **Paging (`paging.c`)**: two-level 32-bit paging with 4 KiB pages. All usable physical memory is identity-mapped, so page tables and the heap's backing frames stay reachable. CR0.WP is set. `paging_map_page`, `paging_unmap_page`, `paging_translate` are the API.
3. **Kernel heap (`kheap.c`)**: first-fit free-list allocator with 16-byte alignment, block splitting, coalescing with both neighbours, and magic numbers for double-free / wild-pointer detection. It lives at virtual `0xD0000000` and grows on demand by asking a callback to map more frames (kernel: `pmm_alloc_frame` + `paging_map_page`). `kheap_check()` walks the block chain and verifies it.

Interrupts: `isr_common` in `boot.s` pushes registers and calls `isr_handler` in C. Page faults print CR2 and exit; the timer increments a tick counter; the keyboard feeds a ring buffer.

## Differences from the AI-generated plan this was based on
The plan's code was not copied. Issues found and handled differently here:
- Plan puts the PMM bitmap right after the kernel in free memory; this uses a static bitmap and reserves regions explicitly.
- Plan's heap init allocates 256 frames one by one and assumes they are contiguous; this maps frames into a contiguous virtual range, which does not need contiguity.
- Plan's paging maps 4096 pages (16 MiB) while its comment says 4 MiB; this maps all usable RAM up to 256 MiB.
- Plan's `pmm_free_frame` frees any set bit, including kernel frames; this tracks ownership.
- Plan has no tests; see below.

## Tests
- `make test-host`: PMM and heap compiled for the host with ASan/UBSan, including a 10,000-operation randomized stress test with content verification.
- `make test-qemu`: boots the kernel, drives the shell over serial, runs the in-kernel `selftest`, checks growth, page fault handling and exit codes.
