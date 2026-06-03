#include "mxu.h"
#include <cstdio>
#include <cstring>

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

// Helpers for MXU1 data types
static inline s32 s16_lo(u32 v) { return (s32)(s16)(v & 0xFFFF); }
static inline s32 s16_hi(u32 v) { return (s32)(s16)((v >> 16) & 0xFFFF); }
static inline u32 u16_lo(u32 v) { return v & 0xFFFF; }
static inline u32 u16_hi(u32 v) { return (v >> 16) & 0xFFFF; }

void MXU::exec_custom(u32 insn) {
    int rt = (insn >> 16) & 0x1F;
    int rs = (insn >> 21) & 0x1F;
    int rd = (insn >> 11) & 0x1F;
    int sa = (insn >> 6) & 0x1F;
    int op = insn & 0x3F;

    switch (op) {
    case 0x01: {
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
        s32 a = s16_lo(state.xregs[rs]);
        s32 b = s16_lo(state.xregs[rd]);
        s64 result = (s64)state.acc[0] + (s64)a * b;
        state.acc[0] = (u32)(result & 0xFFFFFFFF);
        state.acc[1] = (u32)((result >> 32) & 0xFFFFFFFF);
        if (rt) state.xregs[rt] = (u32)result;
        break;
    }
    case 0x08: {
        int shift = sa & 0x1F;
        u32 val = state.xregs[rs] << shift;
        u32 mask = ~(((u32)0xFFFFFFFF) << shift);
        state.xregs[rd] = (state.xregs[rd] & ~mask) | (val & mask);
        if (rt) state.xregs[rt] = state.xregs[rd];
        break;
    }
    case 0x09: {
        int shift = sa & 0x1F;
        u32 val = state.xregs[rs] >> shift;
        u32 mask = ((u32)0xFFFFFFFF) >> shift;
        state.xregs[rd] = (state.xregs[rd] & ~mask) | (val & mask);
        if (rt) state.xregs[rt] = state.xregs[rd];
        break;
    }
    case 0x0B: {
        if (state.xregs[rs] >= state.xregs[rd])
            state.xregs[rt] = state.xregs[rs];
        else
            state.xregs[rt] = state.xregs[rd];
        break;
    }
    case 0x11: {
        s32 a = s16_lo(state.xregs[rs]);
        s32 b = s16_lo(state.xregs[rd]);
        s64 result = (s64)a * b;
        state.acc[0] = (u32)(result & 0xFFFFFFFF);
        state.acc[1] = (u32)((result >> 32) & 0xFFFFFFFF);
        if (rt) state.xregs[rt] = (u32)result;
        break;
    }
    case 0x14: {
        s32 a = s16_lo(state.xregs[rs]);
        s32 b = s16_lo(state.xregs[rd]);
        s64 result = (s64)state.acc[0] + (s64)a * b;
        state.acc[0] = (u32)(result & 0xFFFFFFFF);
        state.acc[1] = (u32)((result >> 32) & 0xFFFFFFFF);
        if (rt) state.xregs[rt] = (u32)result;
        break;
    }
    case 0x19: {
        if (state.xregs[rd] != 0)
            state.xregs[rt] = (u32)((s32)state.xregs[rs] / (s32)state.xregs[rd]);
        else
            state.xregs[rt] = 0;
        break;
    }
    case 0x1B: {
        s64 result = (s64)state.acc[0] + (s32)state.xregs[rs];
        state.acc[0] = (u32)(result & 0xFFFFFFFF);
        state.acc[1] = (u32)((result >> 32) & 0xFFFFFFFF);
        if (rt) state.xregs[rt] = (u32)result;
        break;
    }
    case 0x1E: {
        state.xregs[rt] = state.acc[0];
        break;
    }
    default:
        printf("[MXU] Unknown custom op 0x%02X rs=%d rd=%d rt=%d sa=%d\n",
               op, rs, rd, rt, sa);
        break;
    }
}

// ── MXU1 dispatch (SPECIAL2 opcode 0x1C, selected by func field) ──────

void MXU::exec_mxu1(u32 insn) {
    int func = insn & 0x3F;
    int rs  = (insn >> 21) & 0x1F;
    int rt  = (insn >> 16) & 0x1F;
    int rd  = (insn >> 11) & 0x1F;
    int sa  = (insn >>  6) & 0x1F;

    // Pool sub-opcode selectors
    int pool24_22 = (insn >> 22) & 7;  // 3 bits at 24:22
    int pool23_21 = (insn >> 21) & 7;  // 3 bits at 23:21
    int pool23_22 = (insn >> 22) & 3;  // 2 bits at 23:22

#define XR(i) state.xregs[(i) & 15]

    switch (func) {

    // ── Multiply-accumulate ──────────────────────────────────────
    case 0x00: { // S32MADD: acc += (s32)rs * (s32)rt
        s64 result = (s64)state.acc[0] + (s64)(s32)XR(rs) * (s64)(s32)XR(rt);
        state.acc[0] = (u32)result;
        state.acc[1] = (u32)(result >> 32);
        if (rd) XR(rd) = (u32)result;
        break;
    }
    case 0x01: { // S32MADDU: acc += (u32)rs * (u32)rt
        u64 result = (u64)state.acc[0] + (u64)XR(rs) * (u64)XR(rt);
        state.acc[0] = (u32)result;
        state.acc[1] = (u32)(result >> 32);
        if (rd) XR(rd) = (u32)result;
        break;
    }
    case 0x04: { // S32MSUB: acc -= (s32)rs * (s32)rt
        s64 result = (s64)state.acc[0] - (s64)(s32)XR(rs) * (s64)(s32)XR(rt);
        state.acc[0] = (u32)result;
        state.acc[1] = (u32)(result >> 32);
        if (rd) XR(rd) = (u32)result;
        break;
    }
    case 0x05: { // S32MSUBU: acc -= (u32)rs * (u32)rt
        u64 result = (u64)state.acc[0] - (u64)XR(rs) * (u64)XR(rt);
        state.acc[0] = (u32)result;
        state.acc[1] = (u32)(result >> 32);
        if (rd) XR(rd) = (u32)result;
        break;
    }

    // ── Pool ops ─────────────────────────────────────────────────
    case 0x03: // pool00: bits 24:22 select
        switch (pool24_22) {
        case 0: XR(rd) = XR(rs) >= XR(rt) ? XR(rs) : XR(rt); break; // S32MAX
        case 1: XR(rd) = XR(rs) <= XR(rt) ? XR(rs) : XR(rt); break; // S32MIN
        case 2: { // D16MAX
            u16 hi = s16_hi(XR(rs)) >= s16_hi(XR(rt)) ? u16_hi(XR(rs)) : u16_hi(XR(rt));
            u16 lo = s16_lo(XR(rs)) >= s16_lo(XR(rt)) ? u16_lo(XR(rs)) : u16_lo(XR(rt));
            XR(rd) = ((u32)hi << 16) | lo;
            break;
        }
        case 3: { // D16MIN
            u16 hi = s16_hi(XR(rs)) <= s16_hi(XR(rt)) ? u16_hi(XR(rs)) : u16_hi(XR(rt));
            u16 lo = s16_lo(XR(rs)) <= s16_lo(XR(rt)) ? u16_lo(XR(rs)) : u16_lo(XR(rt));
            XR(rd) = ((u32)hi << 16) | lo;
            break;
        }
        default:
            goto unknown;
        }
        break;

    case 0x06: // pool01: bits 24:22 select
        switch (pool24_22) {
        case 0: XR(rd) = (s32)XR(rs) < (s32)XR(rt) ? 1 : 0; break; // S32SLT
        case 1: XR(rd) = (s16_lo(XR(rs)) < s16_lo(XR(rt)) ? 1u : 0u)
                       | (s16_hi(XR(rs)) < s16_hi(XR(rt)) ? 0x10000u : 0u); break; // D16SLT
        case 2: { // D16AVG
            XR(rd) = ((u16_hi(XR(rs)) + u16_hi(XR(rt)) + 1) >> 1) << 16
                   | ((u16_lo(XR(rs)) + u16_lo(XR(rt)) + 1) >> 1);
            break;
        }
        case 3: { // D16AVGR
            XR(rd) = ((u16_hi(XR(rs)) + u16_hi(XR(rt))) >> 1) << 16
                   | ((u16_lo(XR(rs)) + u16_lo(XR(rt))) >> 1);
            break;
        }
        default:
            goto unknown;
        }
        break;

    case 0x07: // pool02: bits 23:22 select
        switch (pool23_22) {
        case 0: // S32CPS: clip and select
            XR(rd) = (s32)XR(rs) > (s32)XR(rt) ? XR(rt) : XR(rs);
            break;
        case 1: { // D16CPS
            s32 a_lo = s16_lo(XR(rs)), a_hi = s16_hi(XR(rs));
            s32 b_lo = s16_lo(XR(rt)), b_hi = s16_hi(XR(rt));
            XR(rd) = ((a_hi > b_hi ? (u16)b_hi : (u16)a_hi) << 16)
                   | (a_lo > b_lo ? (u16)b_lo : (u16)a_lo);
            break;
        }
        case 2: { // Q8ABD: 8-bit absolute difference
            u32 a = XR(rs), b = XR(rt);
            XR(rd) = 0;
            for (int i = 0; i < 4; i++) {
                int va = (s8)(a >> (i*8)), vb = (s8)(b >> (i*8));
                int d = va - vb;
                XR(rd) |= ((u32)(d < 0 ? -d : d) << (i*8));
            }
            break;
        }
        case 3: { // Q16SAT: parallel 16-bit saturate
            s32 lo = s16_lo(XR(rs)), hi = s16_hi(XR(rs));
            if (lo < -32768) lo = -32768;
            if (lo > 32767) lo = 32767;
            if (hi < -32768) hi = -32768;
            if (hi > 32767) hi = 32767;
            XR(rd) = ((u32)(u16)hi << 16) | (u32)(u16)lo;
            break;
        }
        }
        break;

    // ── 16-bit parallel multiply / MAC ───────────────────────────
    case 0x08: { // D16MUL
        s64 lo = (s64)s16_lo(XR(rs)) * (s64)s16_lo(XR(rt));
        s64 hi = (s64)s16_hi(XR(rs)) * (s64)s16_hi(XR(rt));
        state.acc[0] = (u32)(lo & 0xFFFFFFFF);
        state.acc[1] = (u32)(hi & 0xFFFFFFFF);
        if (rd) {
            XR(rd) = ((u32)(hi & 0xFFFFFFFF)) | (lo & 0xFFFFFFFF); // ?? depends on acc format
        }
        break;
    }
    case 0x09: // pool03: D16MULF, D16MULE (fixed-point variants)
        printf("[MXU1] pool03 (D16MULF/MULE) not implemented\n");
        break;

    case 0x0A: { // D16MAC
        s64 lo = (s64)state.acc[0] + (s64)s16_lo(XR(rs)) * (s64)s16_lo(XR(rt));
        s64 hi = (s64)state.acc[1] + (s64)s16_hi(XR(rs)) * (s64)s16_hi(XR(rt));
        state.acc[0] = (u32)(lo & 0xFFFFFFFF);
        state.acc[1] = (u32)((lo >> 32) & 0xFFFFFFFF);
        // hi low word written to acc[2] on real hardware
        state.acc[2] = (u32)(hi & 0xFFFFFFFF);
        if (rd) XR(rd) = (u32)(lo & 0xFFFFFFFF);
        break;
    }
    case 0x0B: { // D16MACF: acc += D16 product with fixed-point rounding
        s64 lo = (s64)state.acc[0] + (s64)s16_lo(XR(rs)) * (s64)s16_lo(XR(rt));
        s64 hi = (s64)state.acc[1] + (s64)s16_hi(XR(rs)) * (s64)s16_hi(XR(rt));
        lo *= 2; hi *= 2;
        state.acc[0] = (u32)(lo & 0xFFFFFFFF);
        state.acc[1] = (u32)((lo >> 32) & 0xFFFFFFFF);
        break;
    }
    case 0x0C: { // D16MADL: 16-bit multiply-add low
        s64 result = (s64)state.acc[0] + (s64)s16_lo(XR(rs)) * (s64)s16_lo(XR(rt));
        state.acc[0] = (u32)(result & 0xFFFFFFFF);
        state.acc[1] = (u32)((result >> 32) & 0xFFFFFFFF);
        if (rd) XR(rd) = (u32)(result & 0xFFFFFFFF);
        break;
    }
    case 0x0D: { // S16MAD: single 16-bit multiply-add
        s64 result = (s64)state.acc[0] + (s64)s16_lo(XR(rs)) * (s64)s16_lo(XR(rt));
        state.acc[0] = (u32)(result & 0xFFFFFFFF);
        state.acc[1] = (u32)((result >> 32) & 0xFFFFFFFF);
        if (rd) XR(rd) = (u32)(result & 0xFFFFFFFF);
        break;
    }
    case 0x0E: { // Q16ADD: parallel 16-bit add
        s32 l = s16_lo(XR(rs)) + s16_lo(XR(rt));
        s32 h = s16_hi(XR(rs)) + s16_hi(XR(rt));
        XR(rd) = ((u32)(u16)h << 16) | (u32)(u16)l;
        break;
    }
    case 0x0F: { // D16MACE: 16-bit MAC extended
        s64 lo = (s64)state.acc[0] + (s64)s16_lo(XR(rs)) * (s64)s16_lo(XR(rt));
        s64 hi = (s64)state.acc[2] + (s64)s16_hi(XR(rs)) * (s64)s16_hi(XR(rt));
        state.acc[0] = (u32)(lo & 0xFFFFFFFF);
        state.acc[1] = (u32)((lo >> 32) & 0xFFFFFFFF);
        state.acc[2] = (u32)(hi & 0xFFFFFFFF);
        state.acc[3] = (u32)((hi >> 32) & 0xFFFFFFFF);
        break;
    }

    // ── 32-bit / parallel add / accumulate ───────────────────────
    case 0x18: // D32ADD: 32-bit add
        XR(rd) = XR(rs) + XR(rt);
        break;

    case 0x19: // pool12: D32ACC, D32ACCM, D32ASUM
        printf("[MXU1] pool12 (D32ACC/ACCM/ASUM) not implemented\n");
        break;

    case 0x1A: // pool13: Q16ACC, Q16ACCM, Q16ASUM
        printf("[MXU1] pool13 (Q16ACC/ACCM/ASUM) not implemented\n");
        break;

    case 0x1B: // pool14: Q8ADDE, Q8ACCE, D8SUM, D8SUMC
        printf("[MXU1] pool14 (Q8ADDE/ACCE/D8SUM/SUMC) not implemented\n");
        break;

    // ── 32-bit multiply / extract (conflict with CLZ/CLO) ────────
    case 0x20: { // S32MUL (when MXU_EN=1)
        u64 p = (u64)XR(rs) * (u64)XR(rt);
        state.acc[0] = (u32)p;
        state.acc[1] = (u32)(p >> 32);
        if (rd) XR(rd) = (u32)p;
        break;
    }
    case 0x21: { // S32EXTRV: extract from acc
        int shift = XR(rt) & 0x1F;
        u32 val = state.acc[1] << (32 - shift) | (state.acc[0] >> shift);
        if (rd) XR(rd) = val;
        break;
    }

    // ── Register moves (MXU ↔ GPR via SPECIAL2 encoding) ────────
    case 0x2E: // S32M2I: XR[rd] → GPR[rt]
        // Handled in exec_special2
        break;
    case 0x2F: // S32I2M: GPR[rt] → XR[rd]
        // Handled in exec_special2
        break;

    // ── Shifts ───────────────────────────────────────────────────
    case 0x30: // D32SLL: pair shift left logical
        XR(rd) = XR(rs) << sa;
        break;
    case 0x31: // D32SLR: pair shift right logical
        XR(rd) = XR(rs) >> sa;
        break;
    case 0x32: // D32SARL: arithmetic right L
        XR(rd) = (u32)((s32)XR(rs) >> sa);
        break;
    case 0x33: // D32SAR: arithmetic right
        XR(rd) = (u32)((s32)XR(rs) >> sa);
        break;
    case 0x34: // Q16SLL: parallel 16-bit shift left
        XR(rd) = ((u16_lo(XR(rs)) << sa) & 0xFFFF)
               | (((u16_hi(XR(rs)) << sa) & 0xFFFF) << 16);
        break;
    case 0x35: // Q16SLR: parallel 16-bit shift right logical
        XR(rd) = (u16_lo(XR(rs)) >> sa)
               | ((u16_hi(XR(rs)) >> sa) << 16);
        break;
    case 0x37: // Q16SAR: parallel 16-bit shift right arith
        XR(rd) = (u16)(s16_lo(XR(rs)) >> sa)
               | ((u16)(s16_hi(XR(rs)) >> sa) << 16);
        break;

    case 0x27: // pool16: variable shifts
        printf("[MXU1] pool16 (D32/Q16 var shifts) not implemented\n");
        break;

    // ── Bitwise logic (pool15) ───────────────────────────────────
    case 0x26: // pool15: bits 23:21 select
        switch (pool23_21) {
        case 0: // D32SARW
            XR(rd) = (XR(rs) >> (sa & 0x1F)) | (XR(rt) << (32 - (sa & 0x1F)));
            break;
        case 1: XR(rd) = (XR(rs) << 8) | (XR(rt) >> 24); break; // S32ALN (approx)
        case 2: printf("[MXU1] S32ALNI not implemented\n"); break;
        case 3: XR(rd) = sa << 27; break; // S32LUI
        case 4: XR(rd) = ~(XR(rs) | XR(rt)); break; // S32NOR
        case 5: XR(rd) = XR(rs) & XR(rt); break; // S32AND
        case 6: XR(rd) = XR(rs) | XR(rt); break; // S32OR
        case 7: XR(rd) = XR(rs) ^ XR(rt); break; // S32XOR
        }
        break;

    // ── Conditional move ─────────────────────────────────────────
    case 0x36: // pool18: S32MOVZ, S32MOVN, D16MOVZ, D16MOVN
        printf("[MXU1] pool18 (movz/movn) not implemented\n");
        break;
    case 0x39: // pool20: Q8MOVZ, Q8MOVN
        printf("[MXU1] pool20 (Q8 movz/movn) not implemented\n");
        break;

    // ── 8-bit ops ────────────────────────────────────────────────
    case 0x38: // pool19: Q8MUL, Q8MULSU
        printf("[MXU1] Q8MUL not implemented\n");
        break;
    case 0x3A: // pool21: Q8MAC, Q8MACSU
        printf("[MXU1] pool21 (Q8MAC) not implemented\n");
        break;
    case 0x3B: // Q16SCOP
        printf("[MXU1] Q16SCOP not implemented\n");
        break;
    case 0x3C: // Q8MADL
        printf("[MXU1] Q8MADL not implemented\n");
        break;
    case 0x3D: // S32SFL: shuffle
        printf("[MXU1] S32SFL not implemented\n");
        break;
    case 0x3E: // Q8SAD: sum of absolute differences
        printf("[MXU1] Q8SAD not implemented\n");
        break;

    default:
    unknown:
        printf("[MXU1] Unknown/not-implemented func=0x%02X (rs=%d rt=%d rd=%d sa=%d)\n",
               func, rs, rt, rd, sa);
        break;
    }

#undef XR
}
