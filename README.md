# StrataOS: 32-bit x86 Custom Kernel Architecture

[![Language: C](https://img.shields.io/badge/Language-C-blue.svg)](https://en.wikipedia.org/wiki/C_(programming_language))
[![Language: Assembly](https://img.shields.io/badge/Language-x86_Assembly-red.svg)](https://en.wikipedia.org/wiki/X86_assembly_language)
[![Platform: QEMU](https://img.shields.io/badge/Emulator-QEMU-orange.svg)](https://www.qemu.org/)
[![Build: Make](https://img.shields.io/badge/Build-Make-success.svg)](https://www.gnu.org/software/make/)

**StrataOS** (formerly the miniOS Kernel Expansion Project) is a custom, bare-metal 32-bit x86 operating system kernel written in C and Assembly. Built entirely from scratch over an 8-week development lifecycle, this project demonstrates foundational systems engineering concepts including virtual memory paging, preemptive multitasking, hardware privilege rings (Ring 0 / Ring 3), and persistent IDE/ATA storage.

## 🏗️ Core Architecture & Subsystems

The kernel is structured into four primary subsystems, engineered for modularity and stability:

### 1. Memory Management Subsystem
*   **Physical Memory Manager (PMM):** Utilizes a space-efficient, bitmap-based frame allocator to track and distribute 4KB blocks of available RAM parsed from the Multiboot memory map.
*   **Virtual Memory (Paging):** Implements page directories and page tables to map virtual addresses to physical frames, ensuring kernel protection and process isolation.
*   **Dynamic Kernel Heap:** Features a custom first-fit, free-list memory allocator (`kmalloc` and `kfree`) capable of dynamic memory coalescing to prevent fragmentation and resource exhaustion.

### 2. Multitasking & Process Management
*   **Context Switching:** Low-level Assembly routines hook directly into hardware interrupts to save and restore CPU registers seamlessly.
*   **Preemptive Scheduler:** A round-robin task scheduler driven by the Programmable Interval Timer (PIT) at 100 Hz, enabling concurrent time-sliced execution of background threads and the interactive shell.
*   **Process Control Blocks (PCB):** Robust data structures for tracking thread execution states, unique process IDs, and isolated stack pointers.

### 3. User Space & Privilege Isolation
*   **Ring 3 Execution:** Custom Global Descriptor Table (GDT) and Task State Segment (TSS) configuration to securely drop execution privileges from Kernel Mode (Ring 0) to User Mode (Ring 3).
*   **System Call Interface:** A dedicated `int 0x80` software interrupt dispatcher routing unprivileged application requests to secure kernel APIs (e.g., `sys_print`, `sys_yield`), preventing general protection faults.

### 4. Storage & Filesystem
*   **ATA PIO Mode Disk Driver:** Direct port I/O (`inb`/`outb`/`insw`) driver interfacing with the primary IDE controller to read and write 512-byte sectors to a persistent block device.
*   **Flat Filesystem (FFS):** A bespoke filesystem spanning a master directory table and contiguous data sectors, supporting file creation, content retrieval, and persistent storage across power cycles.

---

## ⚙️ Prerequisites

To build and run StrataOS locally, you will need a Linux environment (or WSL on Windows) with the following tools installed:

*   **GCC** (with multilib support for compiling 32-bit binaries: `-m32`)
*   **GNU Make**
*   **QEMU** (specifically `qemu-system-i386`)

**Ubuntu/Debian Installation:**
```bash
sudo apt update
sudo apt install build-essential gcc-multilib qemu-system-x86
```

---

## 🚀 Building and Running

The project includes a `Makefile` configured to compile the freestanding C/Assembly modules, link the ELF kernel, allocate a virtual 10MB raw disk image, and launch the emulator.

```bash
# Clone the repository
git clone https://github.com/yourusername/strataos.git
cd strataos

# Build the kernel, generate disk.img, and boot in QEMU
make run

# To clean build artifacts
make clean
```

---

## 💻 Interactive Shell Commands

Once StrataOS boots successfully, it will mount the filesystem, start the background scheduler, and drop you into an interactive VGA text-mode shell.

| Command | Usage | Description |
| :--- | :--- | :--- |
| `help` | `help` | Lists all available system commands. |
| `uptime` | `uptime` | Displays system uptime based on PIT timer ticks. |
| `meminfo` | `meminfo` | Reports total RAM and currently available physical frames. |
| `alloc` | `alloc <bytes>` | Tests the dynamic heap by allocating contiguous bytes in memory. |
| `free` | `free <hex_addr>` | Frees a previously allocated memory block (e.g., `free 0x101010`). |
| `write` | `write <file> <text>`| Creates a file on the persistent disk with the provided text. |
| `ls` | `ls` | Lists all files currently stored in the Flat Filesystem directory table. |
| `cat` | `cat <file>` | Reads and prints the contents of a file from the disk. |
| `clear` | `clear` | Clears the VGA text buffer. |
| `reboot` | `reboot` | Issues a soft reset to the CPU via the 8042 keyboard controller. |

---

## 📁 Project Structure

```text
├── boot.s       # Assembly entry point, Multiboot header, and interrupt stubs
├── kernel.c     # Main kernel entry, VGA driver, shell, and interrupt handling
├── pmm.c/h      # Physical Memory Manager (Bitmap Allocator)
├── paging.c/h   # Virtual Memory Manager (Page Directory/Tables)
├── kheap.c/h    # Dynamic Kernel Heap (kmalloc / kfree)
├── task.c/h     # Preemptive Round-Robin Scheduler & Task Creation
├── syscall.c/h  # Ring 3 System Call Dispatcher (int 0x80)
├── ata.c/h      # IDE/ATA PIO Mode Disk Driver (LBA28)
├── fs.c/h       # Flat Filesystem Implementation
├── linker.ld    # GNU LD configuration for memory layout (1MB offset)
└── Makefile     # Build system and QEMU execution script
```

---

## 📄 License

This project is open-source and distributed under the [MIT License](LICENSE).
