# Phase 3: user space and system calls

Phase 3 introduces built-in ring-3 tasks, protected kernel mappings and a small `int 0x80` syscall interface. It demonstrates privilege transitions and user-fault containment. It does not provide isolation between user tasks.

Prepared with AI assistance. This documentation describes the published implementation. Source reviewed on 4 October 2026 at commit `db0bc453198fcad0890823d0dc4c46e5fa81fd28`.

## Privilege transition

The GDT contains ring-3 code/data descriptors and a 32-bit TSS. User code selector is `0x1B`; data and stack selector is `0x23`. The scheduler updates the TSS ESP0 to the selected task's kernel-stack top, giving user interrupts a ring-0 entry stack.

User EFLAGS enables interrupts but gives no I/O privilege. The TSS I/O bitmap offset is beyond its limit, so ring-3 port I/O is denied. Only syscall vector 128 has a DPL3 gate; other interrupt gates remain DPL0.

Interrupt entry saves segment registers, switches to kernel segments and clears the direction flag. The restore path returns the selected task's segments and register frame, including syscall results in EAX.

## User memory

Each task slot has a 64 KiB-spaced arena beginning at `0x40000000 + slot * 0x10000`. Only three 4 KiB pages are used:

1. Read-only code copied from the position-independent assembly blob in `src/user.s`.
2. A read/write user stack.
3. A lazy, zero-filled read/write allocation page.

Frames are zeroed before mapping. Kernel, identity and heap page-table entries remain supervisor-only. All tasks share the same page directory and CR3. A user task that knows another user's mapped address can access it. Restricting syscall pointers to the caller's arena does not fix this direct-access limitation.

## Syscall ABI

Use `int 0x80` with the syscall number in EAX and arguments in EBX/ECX. The result is returned in EAX. The interface is defined in `src/syscall.h` and implemented in `src/syscall.c`.

| Number | Call | Arguments and result |
| --- | --- | --- |
| 0 | write | EBX=pointer, ECX=length; at most 256 bytes; returns bytes written |
| 1 | yield | Choose another runnable task; returns 0 |
| 2 | exit | Mark the task dead and switch away |
| 3 | alloc | EBX=1..4096 bytes; returns one zeroed 4 KiB page address |
| 4 | ticks | Return the timer tick counter |
| 5 | sleep | EBX=0..10000 ticks; return 0 after scheduling |

A second allocation fails. There is no user `free` syscall. Write pointers must be in mapped pages of the calling task's arena, with range/overflow checks. Errors include `-1` for the wrong caller, `-12` for allocation unavailable, `-14` for an invalid user range, `-22` for invalid arguments and `-38` for an unknown syscall.

## Fault containment and cleanup

An exception whose saved CS indicates ring 3 marks the task DEAD and resumes a runnable task. Kernel exceptions retain the kernel fault behavior. The shell later reaps the dead user's kernel stack and mapped frames from a different execution context. Empty page tables are retained and reused.

## Syscall entry and return flow

```text
User task: EAX number, EBX/ECX arguments
    -> int 0x80
    -> CPU enters ring 0 using the TSS stack
    -> assembly saves registers and segments, clears DF
    -> syscall_dispatch validates caller and arguments
    -> optional scheduler selects a saved frame
    -> assembly restores context and iret returns to ring 3
```

A console write completes in the kernel and returns the number of bytes printed. Yield and sleep can return through another task first; the original task continues when the scheduler selects its saved frame again. Exit does not resume normal user execution.

Negative errno values are returned through the 32-bit EAX register. Code interpreting the result as unsigned may display a large positive number instead of the negative error; the ABI's error convention is signed.

## User arena offsets

| Relative offset | Mapping | Access |
| --- | --- | --- |
| `+0x0000` | Built-in assembly code page | User read-only |
| `+0x1000` | Stack page | User read/write |
| `+0x2000` | Allocation page, initially absent | User read/write after alloc |

The remaining space between these used pages and the next task's 64 KiB arena is not a general user heap. There is no API for mapping arbitrary pages. `alloc(1)` still supplies one whole zeroed page; subsequent allocation attempts return an error rather than extending it.

## Security boundary: what is and is not checked

The supervisor bit blocks direct user access to kernel mappings. Syscall range checks reject pointers outside the caller's three-page arena and pointers into an allocation page that has not been mapped. The write syscall never trusts an arbitrary user pointer simply because it was passed in a register.

However, all user arenas are present in one shared page directory. Page permissions distinguish kernel from user, not one user task from another. This means direct inter-user memory access remains possible. No claim of process confidentiality or hostile-user isolation is justified.

## Probe behavior and diagnosis

The seven `user fault N` modes test specific illegal operations. Expect a `user fault` report with a task ID, exception vector and error value, followed by a usable shell. They are not examples of ordinary application behavior. A `USER DEMO FAIL` or an unexpected vector is a test failure to investigate, not a condition to ignore.

If `user demo` cannot create a task, inspect table capacity and memory availability. If a spinner is running, find its actual ID before killing it. Repeated demo runs are checked for resource reclamation after the first user page table has been created; retained paging tables explain why the first run may establish a different baseline.

## Try it in QEMU

Build and run with `make` and `make run`. At the kernel shell:

```text
user demo
user spin
tasks
user fault 1
heapcheck
selftest
```

`user demo` exercises privilege level, segment state, syscall results, pointer checks, allocation, sleep, yield and exit; success prints `USER DEMO PASS`. The spinner never yields voluntarily. Stop it with `kill ID` using its ID from `tasks`.

Fault modes 1-7 deliberately attempt: supervisor heap access, CLI, port I/O, code modification, a privileged interrupt, a null read and a bad user stack. These are probes that should terminate their task while leaving the kernel shell usable.

## Verification and limits

The QEMU harness covers demo execution, syscall errors, seven expected fault vector/error pairs, spinner preemption, repeated exit/reclamation, simultaneous users and table saturation. The earlier Phase 3 report had 42 assertions across 10 boots. The later published Phase 4 build reported 61 assertions and 11,596 host checks cumulatively. No tests were rerun for this documentation-only change.

There is no ELF/external program loader, per-process CR3, NX protection, guard page, signal system or general-purpose userland. The Phase 4 filesystem is available only through the kernel shell, not through user syscalls. macOS, real hardware and GitHub Actions execution remain unverified. Do not present this milestone as a secure multi-user OS.

## Source and related documentation

Primary files: `src/syscall.c`, `src/syscall.h`, `src/user.s`, `src/task.c`, `src/interrupt.h`, `src/boot.s`, `src/kernel.c` and `tests/qemu_test.py`.

[Architecture](ARCHITECTURE.md) | [Phase 1](PHASE_1.md) | [Phase 2](PHASE_2.md) | [Phase 4](PHASE_4.md) | [Project README](../README.md)
