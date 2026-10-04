# Phase 4: disk storage and StrataFS

Phase 4 adds a polled ATA disk driver, a custom filesystem with directories and kernel-shell file commands. Storage is implemented in the current repository; the older Phases 1-3 summary describes the historical pre-storage build.

Prepared with AI assistance. This documentation describes the published implementation. Source reviewed on 4 October 2026 at commit `db0bc453198fcad0890823d0dc4c46e5fa81fd28`.

## ATA disk driver

`src/ata.c` and `src/ata.h` support the primary IDE-bus master using PIO, 512-byte sectors and LBA28 addressing. The driver probes with IDENTIFY, records the model and sector count, rejects unsupported signatures/non-LBA devices and clamps the reported capacity to its LBA28 range.

Polling loops have finite limits. Missing disks, timeouts, device errors and out-of-range LBAs return negative error codes instead of spinning forever. Each transfer saves and disables interrupts, then restores their previous state, so the scheduler cannot interleave controller operations. Writes flush the drive cache. There is no DMA, AHCI driver, interrupt-driven I/O or multi-disk selection.

## StrataFS layout

`src/fs.c` is filesystem logic over read/write sector callbacks. Host tests can therefore run the same code against a RAM disk.

| Location | Contents |
| --- | --- |
| LBA 0 | `STRATAFS` superblock, version 1 and layout fields |
| LBA 1-8 | 64 directory entries, 64 bytes each |
| LBA 9 onward | File data |

The root is implicit. Files and directories share the 64-entry limit. Entries record a name, type, parent, size and extent. Names contain at most 31 printable ASCII characters, without whitespace or `/`; `.` and `..` are path components, not valid entry names. Files hold at most 65,536 bytes in one contiguous extent. Paths can be absolute or relative and support `.` and `..`.

## Mounting and write safety

Mounting only reads. It checks the superblock and directory metadata, including types, names, parent chains, duplicate sibling names, file bounds and overlapping extents. Inconsistent metadata is refused without repair or auto-format. A blank or non-StrataFS disk is not formatted automatically.

Replacing a file first allocates space separate from every existing extent, including its own old data. It writes the new data before committing the updated directory sector. A data-write failure before that commit leaves the old file referenced. A failed directory write restores the in-memory entry, but this is not a guarantee against a partial on-disk sector write or a power cut.

`format yes` is destructive: it clears the directory table and writes a new superblock. It is not secure erasure of old data sectors. Never run it on a disk whose contents you want to keep.

## Build and disk image

`make` builds `build/kernel.elf`. `make run` creates a 10 MiB `disk.img` only when missing and attaches it to QEMU as the primary IDE master. It does not overwrite an existing image. `make clean` removes `build/`, not `disk.img`. Image files are ignored by Git.

Linux needs make, a 32-bit-capable GCC and QEMU. `make CC=i686-elf-gcc` is the documented macOS cross-toolchain route, but it has not been verified.

## Filesystem API and common errors

| API | Role |
| --- | --- |
| `fs_mount(device)` | Read metadata, validate and mount without writing |
| `fs_format(device)` | Clear entries, write superblock and mount |
| `fs_write(path,data,len)` | Create or replace a complete file |
| `fs_read(path,buf,max,&size)` | Read a file if the buffer is large enough |
| `fs_mkdir(path)` | Add a directory entry |
| `fs_remove(path)` | Remove a file or eligible empty directory |
| `fs_chdir(path)` / `fs_getcwd` | Change or report the current directory |
| `fs_usage(...)` | Count entries and free/total data sectors |

Common filesystem errors distinguish missing entries, duplicate names, wrong entry type, full storage/table, invalid paths, nonempty directories, protected current-directory ancestry and files over the size limit. A read into too small a buffer returns `FS_ETOOBIG`; it is not an automatic partial-read interface.

ATA errors distinguish no usable device, polling timeout, device error and out-of-range LBA. Filesystem sector failures become `FS_EIO`. Neither layer silently repairs a disk after an error.

## Persistence and replacement example

After writing `docs/note.txt`, stop QEMU and boot again with the same `disk.img`. The filesystem mounts its stored metadata; `cat docs/note.txt` should show the saved text. Do not format again to check persistence, since that removes the directory entries you intend to inspect.

Replacing a 64 KiB file requires another suitable run of 128 sectors while the old run remains allocated. Even replacing a file with the same-size contents can fail if there is no separate run available. This is a tradeoff of the data-first replacement scheme, not a promise that overwrite always succeeds on a full disk.

## Safe troubleshooting

| Symptom | Meaning and next step |
| --- | --- |
| `no ATA disk` | No supported primary-master disk was found; check QEMU attachment |
| `no filesystem mounted` on a new image | Blank/unrecognized image; format only if intentionally disposable |
| Metadata inconsistent | Mount was refused; keep the image if diagnosis matters, do not auto-format |
| `no space left` | Entry table or contiguous data allocation is exhausted |
| `directory not empty` | Remove contents deliberately before removing that directory |
| `verify MISMATCH` | Pattern or expected size differs; not a general checksum of arbitrary content |

Do not attach a valuable disk to this educational kernel. The filesystem has no recovery tools and is not compatible with common host filesystems. Copying an image before an experiment is safer than treating the absence of auto-format as a complete data-protection guarantee.

## Shell walkthrough

Use a fresh disposable QEMU image for this example. Enter lines separately at the StrataOS prompt, without inline comments:

```text
diskinfo
format yes
mkdir docs
write docs/note.txt Hello from StrataOS
ls docs
cat docs/note.txt
cd docs
pwd
cd ..
mkfile sample.bin 1500
verify sample.bin 1500
fsinfo
```

Formatting is first-time setup only, not a step to repeat at every boot. Later boots mount the existing filesystem. The shell's 128-byte line buffer allows at most 127 input characters, including the command and path, so `write` is for short text. `mkfile` generates larger pattern data; `verify` checks that pattern, not arbitrary file content.

Other commands include `ls [path]`, `rm path` and `cd` (with no argument, go to root). Removal accepts a file or an empty directory; root, the current directory and its ancestors cannot be removed. `cat` displays nonprintable bytes as `?`, except newline. `write` creates or replaces a whole file, not an append.

## Verification and limits

The previously reported published build passed 11,596 host checks under ASan/UBSan and 61 QEMU assertions across 15 boots. Coverage includes directory paths, metadata corruption, write-failure injection, no disk, blank disk, persistence, corruption refusal and disk-full cases. The QEMU harness uses disposable images under `build/`, not the normal `disk.img`. No tests were rerun for this documentation-only change.

There is no journal, power-loss guarantee, block cache, defragmentation, timestamps, permissions, FAT compatibility or user-program file API. Replacement needs free space for a new extent before the old one is released. Fragmentation can therefore cause no-space errors despite free sectors. The shell is the only filesystem caller; general concurrent access is not implemented. macOS, physical hardware and GitHub Actions execution remain unverified.

## Source and related documentation

Primary files: `src/ata.c`, `src/ata.h`, `src/fs.c`, `src/fs.h`, `src/kernel.c`, `Makefile`, `tests/host_test.c` and `tests/qemu_test.py`.

[Architecture](ARCHITECTURE.md) | [Phase 1](PHASE_1.md) | [Phase 2](PHASE_2.md) | [Phase 3](PHASE_3.md) | [Project README](../README.md)
