#!/usr/bin/env python3
"""Boot build/kernel.elf in QEMU, drive the shell over the serial port, check output."""
import os, subprocess, sys, time, select

QEMU = os.environ.get("QEMU", "qemu-system-i386")
KERNEL = os.path.join(os.path.dirname(__file__), "..", "build", "kernel.elf")

def run(commands, mem=128, wait_exit=True, timeout=30, disk=None):
    drive = ["-drive", "file=%s,format=raw,if=ide,index=0" % disk] if disk else []
    p = subprocess.Popen(
        [QEMU, "-kernel", KERNEL, "-m", str(mem), "-display", "none", "-serial", "stdio",
         "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04", "-no-reboot", "-nic", "none"] + drive,
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out = b""
    def drain(t):
        nonlocal out
        end = time.time() + t
        while time.time() < end:
            r, _, _ = select.select([p.stdout], [], [], 0.05)
            if r:
                d = os.read(p.stdout.fileno(), 65536)
                if not d: return
                out += d
    drain(1.5)
    for c in commands:
        p.stdin.write((c + "\n").encode()); p.stdin.flush()
        drain(0.6)
    drain(1.0)
    try:
        code = p.wait(timeout=timeout if wait_exit else 0.1)
    except subprocess.TimeoutExpired:
        p.kill(); code = None
    drain(0.1)
    return out.decode(errors="replace"), code

fails = 0
def check(cond, what):
    global fails
    print(("PASS " if cond else "FAIL ") + what)
    if not cond: fails += 1

out, code = run(["help", "meminfo", "selftest", "frame", "frame", "unframe", "malloc 100", "malloc 5000",
                 "free 0", "free 1", "heapcheck", "translate 0x100000", "translate 0xE0000000",
                 "malloc 2000000", "meminfo", "free 0", "uptime", "halt"])
print(out)
check("StrataOS booting" in out, "boots")
check("[ok] Physical memory manager" in out and "[ok] Paging enabled" in out and "[ok] Kernel heap" in out, "phase 1 subsystems initialise")
check("SELFTEST PASS" in out and "FAIL:" not in out, "in-kernel selftest passes")
check("frame 0x" in out and "freed 0x" in out, "frame alloc/free commands")
check("id 0 = 0xd0" in out and "id 1 = 0xd0" in out, "kmalloc returns heap virtual addresses")
check("heap OK" in out, "heapcheck OK")
check("0x00100000 -> 0x00100000" in out, "identity mapping verified")
check("0xe0000000 not mapped" in out, "unmapped address reported")
check("kmalloc failed" not in out and "2793472 bytes mapped" in out, "2 MB allocation grows heap past the initial 256 KiB")
check("ticks:" in out, "timer runs")
check(code == 33, "clean halt via debug-exit (status %s, expected 33)" % code)

out, code = run(["crash"])
check("PAGE FAULT accessing 0xe0000000" in out, "page fault handler reports faulting address")
check(code == 35, "page fault exits with status 35 (got %s)" % code)

out, code = run(["selftest", "halt"], mem=32)
check("SELFTEST PASS" in out and code == 33, "selftest also passes with 32 MB RAM")
out, code = run(["spawn blink", "spawn count 5", "tasks", "meminfo", "heapcheck", "uptime", "malloc 64", "free 0", "tasks", "kill 2", "kill 1", "kill 99", "tasks", "spawn churn 100", "selftest", "tasks", "halt"])
print(out)
check("[ok] Preemptive multitasking" in out, "phase 2 scheduler initialises")
check("started task 2" in out and "started task 3" in out, "spawn starts tasks with ids 2 and 3")
check("blink" in out and "main" in out and "ID  STATE" in out, "tasks command lists main and blink")
check("heap OK" in out and "id 0 = 0xd0" in out, "shell stays responsive and heap works while workers run")
check("killed" in out and "no such running task" in out, "kill works; killing main / unknown id refused")
check(out.count("SELFTEST PASS") == 1 and "FAIL:" not in out, "selftest passes while a blink task is running")
check(code == 33, "clean halt with tasks running (status %s)" % code)

out, code = run(["spawn blink", "spawn count 1000", "selftest", "tasks", "halt"], timeout=60)
print(out)
tail = out.split("SELFTEST")[-1]
check("SELFTEST PASS" in out and "FAIL:" not in out, "selftest passes with user tasks already running")
check("blink" in tail and "count" in tail, "selftest leaves the user's own tasks running")

out, code = run(["spawn blink", "crash"])
check("PAGE FAULT accessing 0xe0000000" in out and code == 35, "page fault in the shell with tasks running is still reported (status %s)" % code)

# Phase 3 uses real ring-3 instructions, not simulated dispatcher calls.
out, code = run(["user demo", "user spin", "tasks", "uptime", "kill 3",
                 "user fault 1", "user fault 2", "user fault 3", "user fault 4",
                 "user fault 5", "user fault 6", "user fault 7", "heapcheck", "selftest", "halt"])
print(out)
check("[ok] Ring 3 + TSS + int 0x80" in out, "phase 3 initialises")
check("USER DEMO PASS" in out and "USER DEMO FAIL" not in out and "UOK" in out,
      "CPL/segments, DF entry, syscall results, pointer bounds, zeroed allocation, sleep/yield/exit")
check("USER SPIN" in out and "3   ready" in out and "ticks:" in out and "killed" in out,
      "never-yielding ring-3 task is preempted and killable")
expected = [(4,14,5),(5,13,0),(6,13,0),(7,14,7),(8,13,0x102),(9,14,5),(10,14,6)]
for task, vector, err in expected:
    check("user fault: task %d vector=%d err=0x%08x" % (task,vector,err) in out,
          "user fault probe %d contained (vector/error verified)" % (task-3))
check("SELFTEST PASS" in out and "heap OK" in out and code == 33,
      "shell/heap/scheduler survive user faults and exit cleanly")

# Warm a user page table, then verify arena/stacks/allocation frames are all reaped.
commands = ["user demo", "meminfo"] + ["user demo"] * 5 + ["meminfo", "tasks", "halt"]
out, code = run(commands, mem=32)
print(out)
import re
frames = re.findall(r"Physical: (\d+) free", out)
used = re.findall(r"bytes mapped, (\d+) used", out)
check(out.count("USER DEMO PASS") == 6 and code == 33, "repeated ring-3 runs with 32 MB RAM")
check(len(frames) == 2 and frames[0] == frames[1] and len(used) == 2 and used[0] == used[1],
      "user frame and kernel-stack reclamation after exit")
check(out.split("ID  STATE")[-1].count("user") == 0, "completed user tasks reaped")

out, code = run(["user demo\nuser demo\nuser spin", "selftest", "tasks", "kill 4", "heapcheck", "halt"])
print(out)
check(out.count("USER DEMO PASS") == 2 and "USER DEMO FAIL" not in out,
      "two simultaneous user arenas and independent syscall stacks")
check("SELFTEST PASS" in out and "4   ready" in out and "heap OK" in out and code == 33,
      "kernel selftest alongside preempted user spin")

out, code = run(["\n".join(["user spin"] * 16), "tasks",
                 "\n".join("kill %d" % i for i in range(2,17)), "tasks", "user demo", "heapcheck", "halt"])
print(out)
check(out.count("started user task") == 16 and out.count("could not create user task") == 1,
      "user tasks respect fixed table limit; refused create does not leak a stack")
check("USER DEMO PASS" in out and "heap OK" in out and code == 33,
      "all user slots reusable after kill/reap")

# ---------- Phase 4: ATA disk and StrataFS (always on disposable images under build/) ----------
import hashlib
BUILD = os.path.join(os.path.dirname(__file__), "..", "build")
def new_image(name, kib=4096):
    path = os.path.join(BUILD, name)
    with open(path, "wb") as f: f.truncate(kib * 1024)
    return path
def sha(path): return hashlib.sha256(open(path, "rb").read()).hexdigest()

out, code = run(["diskinfo", "ls", "format yes", "write a hi", "halt"])
print(out)
check("No ATA disk attached" in out and "no ATA disk" in out and code == 33, "boots without a disk; disk commands refuse cleanly")

img = new_image("blank.img")
before = sha(img)
out, code = run(["diskinfo", "ls", "write a hi", "mkdir d", "cat a", "format", "fsinfo", "halt"], disk=img)
print(out)
check("ATA disk: QEMU HARDDISK, 8192 sectors" in out and "sectors: 8192 (4 MiB)" in out, "ATA IDENTIFY finds the disk and its size")
check("Disk has no StrataFS; nothing was written" in out and out.count("no filesystem mounted") >= 4, "blank disk is not auto-formatted; fs commands refuse")
check("this erases the file table" in out and sha(img) == before and code == 33, "format needs the explicit 'yes'; blank image bytes unchanged")

out, code = run(["format yes", "ls", "write hello.txt Hello_from_persistent_storage", "write note.txt two words here",
                 "mkdir docs", "write docs/inner.txt inside docs", "ls", "ls docs", "cat hello.txt", "cat note.txt",
                 "cd docs", "pwd", "cat inner.txt", "cd ..", "pwd", "cat docs", "cat nothing", "write bad/name x",
                 "mkfile big 65536", "verify big 65536", "mkfile mid 1500", "verify mid 1500", "mkfile big 65537", "fsinfo", "halt"], disk=img)
print(out)
check("formatted StrataFS" in out and "Hello_from_persistent_storage" in out and "two words here" in out and "inside docs" in out,
      "format, write, cat, subdirectory write")
check("d docs/" in out and "- hello.txt  29 bytes" in out and "- inner.txt  11 bytes" in out, "ls shows files, sizes and directories")
check("/docs\r\n" in out.replace("\n", "\\n") or "\n/docs" in out, "cd and pwd work")
check("error: is a directory" in out and "error: no such file or directory" in out, "cat of directory / missing file report errors")
check(out.count("verify OK") == 2 and "wrote 65536 bytes" in out, "64 KiB and 1500-byte files round-trip through the disk")
check("error: file too large" in out and code == 33, "oversize file refused")

out, code = run(["ls", "ls docs", "cat hello.txt", "cat docs/inner.txt", "verify big 65536", "verify mid 1500", "verify mid 1501",
                 "write hello.txt replaced_text", "cat hello.txt", "rm docs", "rm docs/inner.txt", "rm docs", "rm big", "ls", "fsinfo", "halt"], disk=img)
print(out)
check("StrataFS mounted" in out and "Hello_from_persistent_storage" in out and "inside docs" in out, "files survive a power cycle (second boot)")
check(out.count("verify OK") == 2 and "verify MISMATCH" in out, "multi-sector data persists byte-for-byte; wrong size detected")
check("error: directory not empty" in out and "removed" in out, "rm refuses non-empty directory, removes file and empty directory")

out, code = run(["ls", "cat hello.txt", "cat docs", "verify big 65536", "halt"], disk=img)
print(out)
check("replaced_text" in out and "no such file" in out, "replace and delete persisted across another boot")
raw = open(img, "rb").read()
check(raw[:8] == b"STRATAFS" and b"replaced_text" in raw, "image holds the on-disk superblock and file data")

img2 = new_image("corrupt.img")
out, code = run(["format yes", "write a hello", "halt"], disk=img2)
data = bytearray(open(img2, "rb").read()); data[512 + 32] = 9           # invalid entry type in first directory slot
open(img2, "wb").write(data)
before = sha(img2)
out, code = run(["ls", "write b x", "format", "halt"], disk=img2)
print(out)
check("StrataFS not mounted: filesystem metadata is inconsistent. Disk left untouched." in out and out.count("no filesystem mounted") >= 2, "corrupt filesystem is refused, not repaired")
check(sha(img2) == before and code == 33, "corrupt image bytes unchanged by boot and refused commands")

img3 = new_image("small.img", 256)
out, code = run(["format yes", "mkfile a 65536", "mkfile b 65536", "mkfile c 65536", "mkfile d 65536", "fsinfo", "ls", "verify a 65536", "halt"], disk=img3)
print(out)
check("error: no space left" in out and "verify OK" in out, "disk-full refused and earlier files intact on a 256 KiB image")

out, code = run(["selftest", "user demo", "uptime", "uptime", "uptime", "uptime", "halt"], disk=img)
print(out)
check("SELFTEST PASS" in out and "USER DEMO PASS" in out and code == 33, "kernel selftest and ring-3 demo still pass with a disk attached")
for f in ("blank.img", "corrupt.img", "small.img"):
    try: os.remove(os.path.join(BUILD, f))
    except OSError: pass

sys.exit(1 if fails else 0)
