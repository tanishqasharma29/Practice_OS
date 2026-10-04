# Phase 1: memory management

StrataOS is an educational 32-bit x86 kernel in C and assembly. Phase 1 provides physical frame allocation, paging and a kernel heap. These components remain part of the current Phases 1-4 build.

Prepared with AI assistance. This documentation describes the published implementation, not the illustrative code from the original expansion plan. Source reviewed on 4 October 2026 at commit `db0bc453198fcad0890823d0dc4c46e5fa81fd28`.

## Boot and supporting system

QEMU loads the Multiboot v1 kernel. `src/boot.s` sets up the boot stack and calls `kernel_main`. The kernel initializes serial and VGA output, the GDT, IDT, PIC and a 100 Hz PIT timer before setting up memory. PS/2 keyboard and serial input feed the shell.

The Multiboot memory map supplies usable physical regions. If that map is unavailable, the code falls back to the Multiboot upper-memory field. It reserves the first 1 MiB, the kernel image, Multiboot information and the memory-map storage before allocation.

## Physical memory manager

`src/pmm.c` and `src/pmm.h` manage 4 KiB frames below 256 MiB. A static bitmap starts with every frame marked used. Adding a usable region frees only whole frames; reserving a region covers every overlapping frame.

A separate ownership bitmap records frames handed out by the allocator. `pmm_free_frame` rejects unaligned, out-of-range, unowned and already-freed frames. This prevents the API from freeing reserved kernel frames. Allocation returns a physical address, or zero when no frame is available. The API also includes contiguous allocation and counters for free and usable frames.

## Paging

`src/paging.c` and `src/paging.h` implement two-level 32-bit paging with 4 KiB pages. Boot creates an identity map from zero to the highest usable address tracked by the PMM. Kernel mappings are supervisor-only. CR0.PG enables paging and CR0.WP makes the kernel honor read-only mappings.

- `paging_map_page`: map aligned physical and virtual addresses, allocating a page table if needed.
- `paging_unmap_page`: clear a mapping and return its physical frame. The caller handles freeing that frame.
- `paging_translate`: translate a mapped address, including its within-page offset.

Mapping changes invalidate the relevant TLB entry. Empty page tables are retained rather than reclaimed. Kernel page faults report the faulting address; they are not recoverable application errors. Phase 3 adds separate handling for user faults.

## Kernel heap

`src/kheap.c` and `src/kheap.h` provide `kmalloc`, `kfree` and `kheap_check`. The heap starts at virtual address `0xD0000000`, initially maps 256 KiB and can grow to 16 MiB through a callback that allocates and maps backing frames.

It uses a first-fit free list, 16-byte alignment, block splitting and coalescing with neighboring free blocks. Metadata checks reject invalid frees and detect broken block chains. The virtual heap is contiguous; its physical frames need not be. In the current build, interrupt-save/restore hooks protect heap operations from timer preemption.

## How the layers fit together

```text
Multiboot memory information
    -> PMM: allocate physical frames
    -> Paging: map frames to virtual addresses
    -> Heap: return smaller aligned allocations
    -> Kernel subsystems: task stacks and temporary buffers
```

A physical frame is a 4 KiB unit of RAM; a virtual page is an address range mapped to a frame. A heap allocation is a smaller region within mapped heap pages. These APIs are not interchangeable: a `kmalloc` pointer must go to `kfree`, while a PMM frame address must go to `pmm_free_frame` only when the caller no longer needs it.

Heap growth asks for frames one at a time. The kernel maps them into successive virtual pages. If growth fails partway, the growth callback unmaps and frees frames added during that attempt. This avoids assuming that separately allocated physical frames happen to be adjacent.

## API behavior and failure cases

| Operation | Success | Failure or caution |
| --- | --- | --- |
| `pmm_alloc_frame()` | Physical frame address | Zero if exhausted |
| `pmm_alloc_contiguous(n)` | Start of an n-frame run | Zero if no suitable run |
| `pmm_free_frame(address)` | 0 | -1 for invalid or unowned frame |
| `paging_map_page(p,v,flags)` | 0 | -1 for bad alignment or no frame |
| `paging_translate(v,&p)` | 0 and physical address | -1 if not mapped |
| `kmalloc(bytes)` | 16-byte-aligned pointer | NULL for zero size or failure |
| `kfree(pointer)` | 0; NULL is accepted | -1 for detected invalid/double free |
| `kheap_check()` | 0 | -1 for inconsistent block metadata |

Heap checks are diagnostic defenses, not memory safety for arbitrary kernel code. A ring-0 caller can still overwrite memory. Freed heap blocks are coalesced, but freeing an allocation does not shrink the heap mapping back to its original size.

## Reading a memory test result

A successful `heapcheck` prints `heap OK`. `translate 0x100000` should show the identity mapping in the tested QEMU configuration. An unmapped address is reported as not mapped; querying it is different from dereferencing it. The deliberate `crash` probe does dereference an unmapped address and ends the run.

The host harness uses a simulated heap arena, not real x86 paging. That checks allocator logic and metadata under sanitizers. QEMU checks the integrated paging and kernel behavior. Neither route by itself establishes physical-hardware support.

## Try it in QEMU

Build with `make`, then boot with `make run`. The latter also attaches the Phase 4 disk image. At the StrataOS prompt, enter each line separately:

```text
meminfo
frame
unframe
malloc 100
free 0
heapcheck
translate 0x100000
selftest
```

`free 0` assumes the preceding allocation received handle 0 in a fresh session. Otherwise, use the handle printed by `malloc`. `unframe` releases the most recent frame held by the shell. `crash` deliberately triggers a kernel page fault and stops that QEMU run; it is a test, not a recovery command.

## Verification and limits

`make test-host` exercises PMM and heap logic under ASan/UBSan, including randomized allocation and content checks. `make test-qemu` checks initialization, frame allocation, heap growth, address translation and fault reporting.

The published Phase 4 build was previously reported to pass 11,596 host checks and 61 QEMU assertions across all phases. Those totals are cumulative, not Phase 1-only counts; they were not rerun for this documentation-only change. macOS, real hardware and GitHub Actions execution remain unverified. This is not a production memory manager or a secure multi-user OS.

## Source and related documentation

Primary files: `src/pmm.c`, `src/paging.c`, `src/kheap.c`, their headers, `src/kernel.c`, `tests/host_test.c` and `tests/qemu_test.py`.

[Architecture](ARCHITECTURE.md) | [Phase 2](PHASE_2.md) | [Phase 3](PHASE_3.md) | [Phase 4](PHASE_4.md) | [Project README](../README.md)
