#!/usr/bin/env python3
"""Boot build/kernel.elf in QEMU, drive the shell over the serial port, check output."""
import os, subprocess, sys, time, select

QEMU = os.environ.get("QEMU", "qemu-system-i386")
KERNEL = os.path.join(os.path.dirname(__file__), "..", "build", "kernel.elf")

def run(commands, mem=128, wait_exit=True, timeout=30):
    p = subprocess.Popen(
        [QEMU, "-kernel", KERNEL, "-m", str(mem), "-display", "none", "-serial", "stdio",
         "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04", "-no-reboot", "-nic", "none"],
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

sys.exit(1 if fails else 0)
