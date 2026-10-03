#ifndef INTERRUPT_H
#define INTERRUPT_H
#include <stdint.h>
/* Matches boot.s: pusha, four saved segments, vector/error, CPU iret frame.
   useresp/ss are present only when interrupted CS has RPL 3. */
struct regs {
    uint32_t edi, esi, ebp, esp_unused, ebx, edx, ecx, eax;
    uint32_t gs, fs, es, ds, int_no, err, eip, cs, eflags, useresp, ss;
};
#endif
