#include "mxu.h"
#include <cstdio>

void MXU::reset() {
    memset(&state, 0, sizeof(state));
}

u32 MXU::mfc2(int fs) {
    if (fs >= 0 && fs < 16) return state.xregs[fs];
    if (fs == 24) return state.acc[0];
    if (fs == 25) return state.acc[1];
    if (fs == 26) return state.acc[2];
    if (fs == 27) return state.acc[3];
    printf("[MXU] MFC2 unknown fs=%d\n", fs);
    return 0;
}

void MXU::mtc2(int fs, u32 value) {
    if (fs >= 0 && fs < 16) { state.xregs[fs] = value; return; }
    if (fs == 24) { state.acc[0] = value; return; }
    if (fs == 25) { state.acc[1] = value; return; }
    if (fs == 26) { state.acc[2] = value; return; }
    if (fs == 27) { state.acc[3] = value; return; }
    printf("[MXU] MTC2 unknown fs=%d value=0x%08X\n", fs, value);
}

u32 MXU::cfc2(int fs) {
    if (fs == 0) return state.ctrl;
    if (fs == 1) return state.p0;
    if (fs == 2) return state.p1;
    if (fs == 3) return state.p2;
    printf("[MXU] CFC2 unknown fs=%d\n", fs);
    return 0;
}

void MXU::ctc2(int fs, u32 value) {
    if (fs == 0) { state.ctrl = value; return; }
    if (fs == 1) { state.p0 = value; return; }
    if (fs == 2) { state.p1 = value; return; }
    if (fs == 3) { state.p2 = value; return; }
    printf("[MXU] CTC2 unknown fs=%d value=0x%08X\n", fs, value);
}

// Helper: signed 16-bit from lower/upper half
static inline s32 s16_lo(u32 v) { return (s32)(s16)(v & 0xFFFF); }
static inline s32 s16_hi(u32 v) { return (s32)(s16)((v >> 16) & 0xFFFF); }
static inline u32 u16_lo(u32 v) { return v & 0xFFFF; }
static inline u32 u16_hi(u32 v) { return (v >> 16) & 0xFFFF; }

void MXU::exec_custom(u32 insn) {
    // COP2 custom ops: bits 0-5 are the MXU operation code
    // Bits 16-20 = rt, 11-15 = rs, 6-10 = rd
    int rt = (insn >> 16) & 0x1F;
    int rs = (insn >> 21) & 0x1F;
    int rd = (insn >> 11) & 0x1F;
    int sa = (insn >> 6) & 0x1F;
    int op = insn & 0x3F;

    switch (op) {
    case 0x01: {
        // MXU_OP_MADDR: Multiply, add, double, round
        // acc = acc + (rs_lo * rd_lo + rs_hi * rd_hi) * 2
        s32 a = s16_lo(state.xregs[rs]);
        s32 b = s16_lo(state.xregs[rd]);
        s32 c = s16_hi(state.xregs[rs]);
        s32 d = s16_hi(state.xregs[rd]);
        s64 result = (s64)state.acc[0] + ((s64)a * b + (s64)c * d) * 2;
        state.acc[0] = (u32)(result & 0xFFFFFFFF);
        state.acc[1] = (u32)((result >> 32) & 0xFFFFFFFF);
        if (rt) state.xregs[rt] = (u32)result;
        break;
    }
    case 0x03: {
        // MXU_OP_MAD: Multiply-add
        s32 a = s16_lo(state.xregs[rs]);
        s32 b = s16_lo(state.xregs[rd]);
        s64 result = (s64)state.acc[0] + (s64)a * b;
        state.acc[0] = (u32)(result & 0xFFFFFFFF);
        state.acc[1] = (u32)((result >> 32) & 0xFFFFFFFF);
        if (rt) state.xregs[rt] = (u32)result;
        break;
    }
    case 0x08: {
        // MXU_OP_SLINS: Shift left and insert
        int shift = sa & 0x1F;
        u32 val = state.xregs[rs] << shift;
        u32 mask = ~(((u32)0xFFFFFFFF) << shift);
        state.xregs[rd] = (state.xregs[rd] & ~mask) | (val & mask);
        if (rt) state.xregs[rt] = state.xregs[rd];
        break;
    }
    case 0x09: {
        // MXU_OP_SRINS: Shift right and insert
        int shift = sa & 0x1F;
        u32 val = state.xregs[rs] >> shift;
        u32 mask = ((u32)0xFFFFFFFF) >> shift;
        state.xregs[rd] = (state.xregs[rd] & ~mask) | (val & mask);
        if (rt) state.xregs[rt] = state.xregs[rd];
        break;
    }
    case 0x0B: {
        // MXU_OP_CPS: Compare and select
        if (state.xregs[rs] >= state.xregs[rd]) {
            state.xregs[rt] = state.xregs[rs];
        } else {
            state.xregs[rt] = state.xregs[rd];
        }
        break;
    }
    case 0x11: {
        // MXU_OP_MUL: Multiply
        s32 a = s16_lo(state.xregs[rs]);
        s32 b = s16_lo(state.xregs[rd]);
        s64 result = (s64)a * b;
        state.acc[0] = (u32)(result & 0xFFFFFFFF);
        state.acc[1] = (u32)((result >> 32) & 0xFFFFFFFF);
        if (rt) state.xregs[rt] = (u32)result;
        break;
    }
    case 0x14: {
        // MXU_OP_MAC: Multiply-accumulate
        s32 a = s16_lo(state.xregs[rs]);
        s32 b = s16_lo(state.xregs[rd]);
        s64 result = (s64)state.acc[0] + (s64)a * b;
        state.acc[0] = (u32)(result & 0xFFFFFFFF);
        state.acc[1] = (u32)((result >> 32) & 0xFFFFFFFF);
        if (rt) state.xregs[rt] = (u32)result;
        break;
    }
    case 0x19: {
        // MXU_OP_DIV: Divide step
        if (state.xregs[rd] != 0) {
            state.xregs[rt] = (u32)((s32)state.xregs[rs] / (s32)state.xregs[rd]);
        } else {
            state.xregs[rt] = 0;
        }
        break;
    }
    case 0x1B: {
        // MXU_OP_ACC: Accumulate
        s64 result = (s64)state.acc[0] + (s32)state.xregs[rs];
        state.acc[0] = (u32)(result & 0xFFFFFFFF);
        state.acc[1] = (u32)((result >> 32) & 0xFFFFFFFF);
        if (rt) state.xregs[rt] = (u32)result;
        break;
    }
    case 0x1E: {
        // MXU_OP_FIN: Finalize/round
        state.xregs[rt] = state.acc[0];
        break;
    }
    default:
        printf("[MXU] Unknown custom op 0x%02X rs=%d rd=%d rt=%d sa=%d\n",
               op, rs, rd, rt, sa);
        break;
    }
}
