#ifndef MXU_H
#define MXU_H

#include "types.h"

// MXU (COP2) state for JZ4730
struct MXUState {
    u32 xregs[16];   // 16 X registers (32-bit each)
    u32 acc[4];      // 128-bit accumulator (4 × 32-bit)
    u32 ctrl;        // control register
    u32 p0, p1, p2;  // pool registers
};

class MXU {
public:
    MXUState state;

    void reset();

    // MFC2 / MTC2 / CFC2 / CTC2
    u32 mfc2(int fs);
    void mtc2(int fs, u32 value);
    u32 cfc2(int fs);
    void ctc2(int fs, u32 value);

    // Custom MXU operations (opcode from bits 0-5 of SPECIAL2 field)
    void exec_custom(u32 insn);
};

#endif // MXU_H
