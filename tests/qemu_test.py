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
sys.exit(1 if fails else 0)
