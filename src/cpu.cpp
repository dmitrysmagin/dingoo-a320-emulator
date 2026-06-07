#include "cpu.h"
#include <cstdio>
#include <cstring>
#include <ctime>

// Global register access for syscall dispatch
u32 g_cpu_regs[32];
u32 g_cpu_pc;
u32 g_cpu_hi;
u32 g_cpu_lo;



// Sign extend helpers
static inline s32 sext16(u32 v) { return (s32)(s16)v; }
static inline s32 sext26(u32 v) { return (s32)(v << 6) >> 6; }

void CPU::reset() {
    memset(regs, 0, sizeof(regs));
    pc = 0;
    hi = lo = 0;
    llbit = 0;
    ll_addr = 0;
    cop0.reset();
    mxu.reset();
    running = true;
    nullify_delay = false;
    insn_count = 0;
    m_trace_idx = 0;
    memset(m_trace_pc, 0, sizeof(m_trace_pc));
    memset(m_trace_insn, 0, sizeof(m_trace_insn));
}

u32 CPU::fetch() {
    // OS area (0x80000000-0x809FFFFF) has no loaded code - return JR $ra to skip
    if (pc >= 0x80000000 && pc < 0x80A00000) {
        static bool warned = false;
        if (!warned) {
            printf("[CPU] PC in OS area 0x%08X - returning JR $ra\n", pc);
            warned = true;
        }
        return 0x03E00008;  // JR $ra
    }
    return mem->read_u32(pc);
}

void CPU::raise_exception(u32 code) {
    cop0.regs.cause = (cop0.regs.cause & ~0x7C) | (code << 2);
    cop0.regs.epc = pc - 4;
    cop0.regs.bad_vaddr = pc;
    printf("[EXCEPTION] code=%u at PC=0x%08X\n", code, pc);
    running = false;
}

void CPU::exec_special(u32 insn) {
    int rs = (insn >> 21) & 0x1F;
    int rt = (insn >> 16) & 0x1F;
    int rd = (insn >> 11) & 0x1F;
    int sa = (insn >> 6) & 0x1F;
    int func = insn & 0x3F;

    switch (func) {
    case 0x00: if (rd) regs[rd] = regs[rt] << sa; break;
    case 0x02: if (rd) regs[rd] = regs[rt] >> sa; break;
    case 0x03: if (rd) regs[rd] = (u32)((s32)regs[rt] >> sa); break;
    case 0x04: if (rd) regs[rd] = regs[rt] << (regs[rs] & 0x1F); break;
    case 0x06: if (rd) regs[rd] = regs[rt] >> (regs[rs] & 0x1F); break;
    case 0x07: if (rd) regs[rd] = (u32)((s32)regs[rt] >> (regs[rs] & 0x1F)); break;
    case 0x08: pc = regs[rs]; break;  // JR
    case 0x09: { u32 t = regs[rs]; regs[rd] = pc + 4; pc = t; break; }  // JALR (read rs first in case rs==rd)
    case 0x0A: if (rd && regs[rt] == 0) regs[rd] = regs[rs]; break;  // MOVZ
    case 0x0B: if (rd && regs[rt] != 0) regs[rd] = regs[rs]; break;  // MOVN
    case 0x0C: raise_exception(EXC_SYS); break;
    case 0x0D: raise_exception(EXC_BP); break;
    case 0x0F: break;  // SYNC
    case 0x10: if (rd) regs[rd] = hi; break;
    case 0x11: hi = regs[rs]; break;
    case 0x12: if (rd) regs[rd] = lo; break;
    case 0x13: lo = regs[rs]; break;
    case 0x18: { s64 r = (s64)(s32)regs[rs] * (s64)(s32)regs[rt]; lo = (u32)r; hi = (u32)(r >> 32); } break;
    case 0x19: { u64 r = (u64)regs[rs] * (u64)regs[rt]; lo = (u32)r; hi = (u32)(r >> 32); } break;
    case 0x1A: if (regs[rt]) { lo = (u32)((s32)regs[rs] / (s32)regs[rt]); hi = (u32)((s32)regs[rs] % (s32)regs[rt]); } break;
    case 0x1B: if (regs[rt]) { lo = regs[rs] / regs[rt]; hi = regs[rs] % regs[rt]; } break;
    case 0x20: if (rd) regs[rd] = regs[rs] + regs[rt]; break;  // ADD (overflow trap disabled to match MAME default)
    case 0x21: if (rd) regs[rd] = regs[rs] + regs[rt]; break;  // ADDU
    case 0x22: if (rd) regs[rd] = regs[rs] - regs[rt]; break;  // SUB (overflow trap disabled)
    case 0x23: if (rd) regs[rd] = regs[rs] - regs[rt]; break;
    case 0x24: if (rd) regs[rd] = regs[rs] & regs[rt]; break;
    case 0x25: if (rd) regs[rd] = regs[rs] | regs[rt]; break;
    case 0x26: if (rd) regs[rd] = regs[rs] ^ regs[rt]; break;
    case 0x27: if (rd) regs[rd] = ~(regs[rs] | regs[rt]); break;
    case 0x2A: if (rd) regs[rd] = (s32)regs[rs] < (s32)regs[rt] ? 1 : 0; break;
    case 0x2B: if (rd) regs[rd] = regs[rs] < regs[rt] ? 1 : 0; break;
    default:
        static bool warned = false;
        if (!warned) {
            warned = true;
            printf("[CPU] SPECIAL non-standard func=0x%02X at PC=0x%08X insn=0x%08X (treated as NOP, possibly MXU)\n", func, pc - 4, insn);
        }
        break;
    }
}

void CPU::exec_special2(u32 insn) {
    int func = insn & 0x3F;
    int rs = (insn >> 21) & 0x1F;
    int rt = (insn >> 16) & 0x1F;
    int rd = (insn >> 11) & 0x1F;
    bool mxu_en = (mxu.state.ctrl & 1) != 0;

    switch (func) {
    // ── MIPS32r1 SPECIAL2 (conflicts with MXU1 when MXU_EN) ──────
    case 0x00:
        if (mxu_en) { mxu.exec_mxu1(insn); break; }
        {  // MADD: {HI,LO} += signed(rs) * signed(rt)
            s64 acc = ((s64)(s32)hi << 32) | lo;
            acc += (s64)(s32)regs[rs] * (s64)(s32)regs[rt];
            lo = (u32)(acc & 0xFFFFFFFF);
            hi = (u32)((u64)acc >> 32);
        }
        break;
    case 0x01:
        if (mxu_en) { mxu.exec_mxu1(insn); break; }
        {  // MADDU: {HI,LO} += unsigned(rs) * unsigned(rt)
            u64 acc = ((u64)hi << 32) | lo;
            acc += (u64)regs[rs] * (u64)regs[rt];
            lo = (u32)(acc & 0xFFFFFFFF);
            hi = (u32)(acc >> 32);
        }
        break;
    case 0x02:  // MUL rd, rs, rt (MIPS only — no MXU1 conflict)
        if (rd) regs[rd] = regs[rs] * regs[rt];
        break;
    case 0x04:
        if (mxu_en) { mxu.exec_mxu1(insn); break; }
        {  // MSUB: {HI,LO} -= signed(rs) * signed(rt)
            s64 acc = ((s64)(s32)hi << 32) | lo;
            acc -= (s64)(s32)regs[rs] * (s64)(s32)regs[rt];
            lo = (u32)(acc & 0xFFFFFFFF);
            hi = (u32)((u64)acc >> 32);
        }
        break;
    case 0x05:
        if (mxu_en) { mxu.exec_mxu1(insn); break; }
        {  // MSUBU: {HI,LO} -= unsigned(rs) * unsigned(rt)
            u64 acc = ((u64)hi << 32) | lo;
            acc -= (u64)regs[rs] * (u64)regs[rt];
            lo = (u32)(acc & 0xFFFFFFFF);
            hi = (u32)(acc >> 32);
        }
        break;
    case 0x20:
        if (mxu_en) { mxu.exec_mxu1(insn); break; }
        {  // CLZ rd, rs: count leading zeros
            if (regs[rs] == 0) regs[rd] = 32;
            else { u32 v = regs[rs]; int c = 0; while ((v & 0x80000000u) == 0) { v <<= 1; c++; } regs[rd] = c; }
        }
        break;
    case 0x21:
        if (mxu_en) { mxu.exec_mxu1(insn); break; }
        {  // CLO rd, rs: count leading ones
            if (regs[rs] == 0xFFFFFFFF) regs[rd] = 32;
            else { u32 v = regs[rs]; int c = 0; while ((v & 0x80000000u) != 0) { v <<= 1; c++; } regs[rd] = c; }
        }
        break;

    // ── MXU1 memory-load ops ─────────────────────────────────────
    case 0x10: { // S32LDD: XR[rt]  = mem[GPR[rs]+GPR[rd]], XR[rt|1] = mem[addr+4]
        u32 addr = regs[rs] + regs[rd];
        mxu.state.xregs[rt & 15]   = mem->read_u32(addr);
        mxu.state.xregs[(rt|1) & 15] = mem->read_u32(addr + 4);
        break;
    }
    case 0x12: { // S32LDDV: XR[rt..] = mem[GPR[rs] + GPR[rt]<<2] (like LDDV — indexed)
        u32 addr = regs[rs] + (regs[rt] << 2);
        mxu.state.xregs[rd & 15]     = mem->read_u32(addr);
        mxu.state.xregs[(rd|1) & 15] = mem->read_u32(addr + 4);
        break;
    }
    case 0x14: { // S32LDI: XR[rt..] = mem[GPR[rs]+sa], GPR[rd] += sa
        u32 addr = regs[rs] + (s32)sext16((insn & 0xFFFF));
        mxu.state.xregs[rt & 15]   = mem->read_u32(addr);
        mxu.state.xregs[(rt|1) & 15] = mem->read_u32(addr + 4);
        if (rd) regs[rd] += (s32)sext16((insn & 0xFFFF));
        break;
    }
    case 0x16: { // S32LDIV: XR[rd..] = mem[GPR[rs] + GPR[rt]<<2], GPR[rt] += 1
        u32 addr = regs[rs] + (regs[rt] << 2);
        mxu.state.xregs[rd & 15]     = mem->read_u32(addr);
        mxu.state.xregs[(rd|1) & 15] = mem->read_u32(addr + 4);
        regs[rt]++;
        break;
    }

    // ── MXU1 memory-store ops ────────────────────────────────────
    case 0x11: { // S32STD
        u32 addr = regs[rs] + regs[rd];
        mem->write_u32(addr,     mxu.state.xregs[rt & 15]);
        mem->write_u32(addr + 4, mxu.state.xregs[(rt|1) & 15]);
        break;
    }
    case 0x13: { // S32STDV
        u32 addr = regs[rs] + (regs[rt] << 2);
        mem->write_u32(addr,     mxu.state.xregs[rd & 15]);
        mem->write_u32(addr + 4, mxu.state.xregs[(rd|1) & 15]);
        break;
    }
    case 0x15: { // S32SDI
        u32 addr = regs[rs] + (s32)sext16((insn & 0xFFFF));
        mem->write_u32(addr,     mxu.state.xregs[rt & 15]);
        mem->write_u32(addr + 4, mxu.state.xregs[(rt|1) & 15]);
        if (rd) regs[rd] += (s32)sext16((insn & 0xFFFF));
        break;
    }
    case 0x17: { // S32SDIV
        u32 addr = regs[rs] + (regs[rt] << 2);
        mem->write_u32(addr,     mxu.state.xregs[rd & 15]);
        mem->write_u32(addr + 4, mxu.state.xregs[(rd|1) & 15]);
        regs[rt]++;
        break;
    }

    // ── MXU1 byte/halfword loads ─────────────────────────────────
    case 0x22: { // S8LDD
        u32 addr = regs[rs] + regs[rd];
        mxu.state.xregs[rt & 15] = (u32)(s8)mem->read_u8(addr);
        break;
    }
    case 0x24: { // S8LDI
        u32 addr = regs[rs] + (s32)sext16((insn & 0xFFFF));
        mxu.state.xregs[rt & 15] = (u32)(s8)mem->read_u8(addr);
        break;
    }
    case 0x2A: { // S16LDD
        u32 addr = regs[rs] + regs[rd];
        mxu.state.xregs[rt & 15] = (u32)(s16)mem->read_u16(addr);
        break;
    }
    case 0x2C: { // S16LDI
        u32 addr = regs[rs] + (s32)sext16((insn & 0xFFFF));
        mxu.state.xregs[rt & 15] = (u32)(s16)mem->read_u16(addr);
        break;
    }

    // ── MXU1 byte/halfword stores ────────────────────────────────
    case 0x23: { // S8STD
        u32 addr = regs[rs] + regs[rd];
        mem->write_u8(addr, (u8)mxu.state.xregs[rt & 15]);
        break;
    }
    case 0x25: { // S8SDI
        u32 addr = regs[rs] + (s32)sext16((insn & 0xFFFF));
        mem->write_u8(addr, (u8)mxu.state.xregs[rt & 15]);
        break;
    }
    case 0x2B: { // S16STD
        u32 addr = regs[rs] + regs[rd];
        mem->write_u16(addr, (u16)mxu.state.xregs[rt & 15]);
        break;
    }
    case 0x2D: { // S16SDI
        u32 addr = regs[rs] + (s32)sext16((insn & 0xFFFF));
        mem->write_u16(addr, (u16)mxu.state.xregs[rt & 15]);
        break;
    }

    // ── LX: unaligned load to GPR ────────────────────────────────
    case 0x28: { // LX — word/halfword/byte select via bits 24:22
        int lx_type = (insn >> 22) & 3;
        u32 addr = regs[rs] + (s32)sext16((insn & 0xFFFF));
        if (lx_type == 0)      regs[rd] = mem->read_u32(addr);
        else if (lx_type == 1) regs[rd] = (u32)(s16)mem->read_u16(addr);
        else if (lx_type == 2) regs[rd] = (u32)(s8)mem->read_u8(addr);
        else                   regs[rd] = (u32)(s16)mem->read_u16(addr);
        break;
    }

    // ── Register moves MXU ↔ GPR ─────────────────────────────────
    case 0x2E: // S32M2I: XR[rd] → GPR[rt]
        regs[rt] = mxu.state.xregs[rd & 15];
        break;
    case 0x2F: // S32I2M: GPR[rt] → XR[rd]
        mxu.state.xregs[rd & 15] = regs[rt];
        break;

    // ── MXU1 compute ops (no memory) ─────────────────────────────
    case 0x03:
    case 0x06: case 0x07:
    case 0x08: case 0x09: case 0x0A: case 0x0B:
    case 0x0C: case 0x0D: case 0x0E: case 0x0F:
    case 0x18: case 0x19: case 0x1A: case 0x1B:
    case 0x26: case 0x27:
    case 0x30: case 0x31: case 0x32: case 0x33:
    case 0x34: case 0x35: case 0x36: case 0x37:
    case 0x38: case 0x39: case 0x3A: case 0x3B:
    case 0x3C: case 0x3D: case 0x3E:
        mxu.exec_mxu1(insn);
        break;

    default:
        printf("[CPU] SPECIAL2 unknown func=0x%02X at PC=0x%08X insn=0x%08X\n",
               func, pc - 4, insn);
        raise_exception(EXC_RI);
        break;
    }
}

void CPU::exec_special3(u32 insn) {
    int func = insn & 0x3F;
    int rt = (insn >> 16) & 0x1F;
    int rs = (insn >> 21) & 0x1F;
    int rd = (insn >> 11) & 0x1F;
    int sa = (insn >> 6) & 0x1F;

    if (func == 0x00) {
        int pos = sa;
        int size = rd + 1;
        regs[rt] = (regs[rs] >> pos) & ((1u << size) - 1);
    } else if (func == 0x04) {
        int pos = sa;
        int size = rd + 1;
        u32 mask = ((1u << size) - 1) << pos;
        regs[rt] = (regs[rt] & ~mask) | ((regs[rs] & ((1u << size) - 1)) << pos);
    } else {
        printf("[CPU] SPECIAL3 unknown func=0x%02X at PC=0x%08X insn=0x%08X\n", func, pc - 4, insn);
        raise_exception(EXC_RI);
    }
}

void CPU::execute(u32 insn) {
    int opcode = (insn >> 26) & 0x3F;
    int rs = (insn >> 21) & 0x1F;
    int rt = (insn >> 16) & 0x1F;
    s32 imm = (s32)sext16(insn & 0xFFFF);
    u32 uimm = insn & 0xFFFF;

    switch (opcode) {
    case 0x00: exec_special(insn); break;
    case 0x01: {
        int rt_field = (insn >> 16) & 0x1F;
        s32 offset = sext16(insn & 0xFFFF);
        switch (rt_field) {
        case 0x00: if ((s32)regs[rs] < 0) pc = pc + (offset << 2); break;  // BLTZ
        case 0x01: if ((s32)regs[rs] >= 0) pc = pc + (offset << 2); break;  // BGEZ
        case 0x02: if ((s32)regs[rs] < 0) pc = pc + (offset << 2); else nullify_delay = true; break;  // BLTZL
        case 0x03: if ((s32)regs[rs] >= 0) pc = pc + (offset << 2); else nullify_delay = true; break;  // BGEZL
        case 0x10: regs[31] = pc + 4; if ((s32)regs[rs] < 0) pc = pc + (offset << 2); break;  // BLTZAL
        case 0x11: regs[31] = pc + 4; if ((s32)regs[rs] >= 0) pc = pc + (offset << 2); break;  // BGEZAL
        case 0x12: regs[31] = pc + 4; if ((s32)regs[rs] < 0) pc = pc + (offset << 2); else nullify_delay = true; break;  // BLTZALL
        case 0x13: regs[31] = pc + 4; if ((s32)regs[rs] >= 0) pc = pc + (offset << 2); else nullify_delay = true; break;  // BGEZALL
        default: {
            static bool warned = false;
            if (!warned) {
                warned = true;
                printf("[CPU] REGIMM non-standard rt=0x%02X at PC=0x%08X insn=0x%08X\n", rt_field, pc - 4, insn);
            }
            break;
        }
    }
    break;
    }
    case 0x02: {
        u32 target = (insn & 0x03FFFFFF) << 2;
        pc = (pc & 0xF0000000) | target;
        break;
    }
    case 0x03: {
        regs[31] = pc + 4;  // save address AFTER delay slot (A+8)
        u32 target = (insn & 0x03FFFFFF) << 2;
        pc = (pc & 0xF0000000) | target;
        break;
    }
    case 0x04: if (regs[rs] == regs[rt]) pc = pc + (imm << 2); break;  // BEQ
    case 0x05: if (regs[rs] != regs[rt]) pc = pc + (imm << 2); break;  // BNE
    case 0x06: if ((s32)regs[rs] <= 0) pc = pc + (imm << 2); break;  // BLEZ
    case 0x07: if ((s32)regs[rs] > 0) pc = pc + (imm << 2); break;  // BGTZ
    case 0x08: if (rt) regs[rt] = regs[rs] + (u32)imm; break;  // ADDI (overflow trap disabled)
    case 0x09: if (rt) regs[rt] = regs[rs] + (u32)imm; break;
    case 0x0A: if (rt) regs[rt] = (s32)regs[rs] < imm ? 1 : 0; break;
    case 0x0B: if (rt) regs[rt] = regs[rs] < (u32)imm ? 1 : 0; break;
    case 0x0C: if (rt) regs[rt] = regs[rs] & uimm; break;
    case 0x0D: if (rt) regs[rt] = regs[rs] | uimm; break;
    case 0x0E: if (rt) regs[rt] = regs[rs] ^ uimm; break;
    case 0x0F: if (rt) regs[rt] = uimm << 16; break;
    case 0x10: {
        int rs_field = (insn >> 21) & 0x1F;
        int rt_field = (insn >> 16) & 0x1F;
        int rd_field = (insn >> 11) & 0x1F;
        int func = insn & 0x3F;
        if (rs_field == 0x00) regs[rt_field] = cop0.mfc0(rd_field);
        else if (rs_field == 0x04) cop0.mtc0(rd_field, regs[rt_field]);
        else if (rs_field == 0x10) {
            // C0 (TLB / ERET) — function is in bits 5-0
            switch (func) {
            case 0x01: // TLBR — TLB removed; trap
                printf("[CPU] TLBR (unhandled) at PC=0x%08X insn=0x%08X\n", pc - 4, insn);
                raise_exception(EXC_RI);
                break;
            case 0x02: // TLBWI — TLB removed; trap
                printf("[CPU] TLBWI (unhandled) at PC=0x%08X insn=0x%08X\n", pc - 4, insn);
                raise_exception(EXC_RI);
                break;
            case 0x06: // TLBWR — TLB removed; trap
                printf("[CPU] TLBWR (unhandled) at PC=0x%08X insn=0x%08X\n", pc - 4, insn);
                raise_exception(EXC_RI);
                break;
            case 0x08: // TLBP — TLB removed; trap (mark Index as not found)
                printf("[CPU] TLBP (unhandled) at PC=0x%08X insn=0x%08X\n", pc - 4, insn);
                cop0.regs.index = 0x80000000; // bit 31 = 1: not found
                raise_exception(EXC_RI);
                break;
            case 0x18: // ERET
                pc = cop0.regs.epc;
                cop0.regs.status &= ~0x2u;
                llbit = 0;
                ll_addr = 0;
                break;
            default:
                printf("[CPU] COP0 C0 unknown func=0x%02X at PC=0x%08X insn=0x%08X\n", func, pc - 4, insn);
                raise_exception(EXC_RI);
                break;
            }
        }
        break;
    }
    case 0x11:
        printf("[CPU] COP1 (FPU) not implemented at PC=0x%08X insn=0x%08X\n", pc - 4, insn);
        raise_exception(EXC_RI);
        break;
    case 0x12: {
        int rs_field = (insn >> 21) & 0x1F;
        int rt_field = (insn >> 16) & 0x1F;
        int rd_field = (insn >> 11) & 0x1F;
        if (rs_field == 0x00) regs[rt_field] = mxu.mfc2(rd_field);
        else if (rs_field == 0x04) mxu.mtc2(rd_field, regs[rt_field]);
        else if (rs_field == 0x02) regs[rt_field] = mxu.cfc2(rd_field);
        else if (rs_field == 0x06) mxu.ctc2(rd_field, regs[rt_field]);
        else mxu.exec_custom(insn);
        break;
    }
    case 0x13:
        printf("[CPU] COP3 not implemented at PC=0x%08X\n", pc - 4);
        raise_exception(EXC_RI);
        break;
    case 0x14: if (regs[rs] == regs[rt]) pc = pc + (imm << 2); else nullify_delay = true; break;  // BEQL
    case 0x15: if (regs[rs] != regs[rt]) pc = pc + (imm << 2); else nullify_delay = true; break;  // BNEL
    case 0x16: if ((s32)regs[rs] <= 0) pc = pc + (imm << 2); else nullify_delay = true; break;  // BLEZL
    case 0x17: if ((s32)regs[rs] > 0) pc = pc + (imm << 2); else nullify_delay = true; break;  // BGTZL
    case 0x1C: exec_special2(insn); break;
    case 0x1F: exec_special3(insn); break;

    // Loads
    case 0x20: if (rt) regs[rt] = (u32)(s8)mem->read_u8(regs[rs] + imm); break;
    case 0x21: if (rt) regs[rt] = (u32)(s16)mem->read_u16(regs[rs] + imm); break;
    case 0x22: {  // LWL (little-endian MIPS: loads high bytes of aligned word into rt)
        u32 addr = regs[rs] + imm;
        u32 aligned = addr & ~3;
        u32 byte_off = addr & 3;
        u32 val = mem->read_u32(aligned);
        if (rt) {
            u32 shift = (3 - byte_off) * 8;
            u32 mask = shift ? (1u << shift) - 1u : 0u;
            regs[rt] = (regs[rt] & mask) | (val << shift);
        }
        break;
    }
    case 0x23: {
        u32 load_addr = regs[rs] + imm;
        u32 load_val = mem->read_u32(load_addr);
        if (rt) regs[rt] = load_val;

        break;
    }
    case 0x24: if (rt) regs[rt] = mem->read_u8(regs[rs] + imm); break;
    case 0x25: if (rt) regs[rt] = mem->read_u16(regs[rs] + imm); break;
    case 0x26: {  // LWR (little-endian): matches MAME lwr_le
        u32 addr = regs[rs] + imm;
        u32 byte_off = addr & 3;
        u32 val = mem->read_u32(addr & ~3);
        if (rt) {
            u32 shift = byte_off * 8u;
            u32 mask = 0xFFFFFFFFu >> shift;
            regs[rt] = (regs[rt] & ~mask) | (val >> shift);
        }
        break;
    }

    // Stores
    case 0x28: {
        u32 store_addr = regs[rs] + imm;
        mem->write_u8(store_addr, (u8)regs[rt]);

        break;
    }
    case 0x29: mem->write_u16(regs[rs] + imm, (u16)regs[rt]); break;
    case 0x2A: {  // SWL (little-endian MIPS: stores high bytes of rt into low part of aligned word)
        u32 addr = regs[rs] + imm;
        u32 aligned = addr & ~3;
        u32 byte_off = addr & 3;
        u32 existing = mem->read_u32(aligned);
        u32 stored_bits = (byte_off + 1u) * 8u;
        if (stored_bits >= 32u) {
            mem->write_u32(aligned, regs[rt]);
        } else {
            u32 mask = (1u << stored_bits) - 1u;
            u32 shift = (3u - byte_off) * 8u;
            mem->write_u32(aligned, (existing & ~mask) | ((regs[rt] >> shift) & mask));
        }
        break;
    }
    case 0x2B:
        mem->write_u32(regs[rs] + imm, regs[rt]);
        break;
    case 0x2E: {  // SWR (little-endian MIPS: stores low bytes of rt into high part of aligned word)
        u32 addr = regs[rs] + imm;
        u32 aligned = addr & ~3;
        u32 byte_off = addr & 3;
        u32 existing = mem->read_u32(aligned);
        u32 shift = byte_off * 8u;
        if (shift == 0u) {
            mem->write_u32(aligned, regs[rt]);
        } else {
            u32 mask = ~((1u << shift) - 1u);
            mem->write_u32(aligned, (existing & ~mask) | (regs[rt] << shift));
        }
        break;
    }

    case 0x2F: {  // CACHE (nop)
        static bool warned = false;
        if (!warned) { warned = true; printf("[CPU] CACHE instruction at PC=0x%08X (ignored)\n", pc - 4); }
        break;
    }
    case 0x30: {  // LL
        u32 addr = regs[rs] + imm;
        if (rt) regs[rt] = mem->read_u32(addr);
        llbit = 1;
        ll_addr = addr;
        break;
    }
    case 0x31: {  // LWC1
        mem->read_u32(regs[rs] + imm);
        static bool warned = false;
        if (!warned) { warned = true; printf("[CPU] LWC1 instruction at PC=0x%08X (load discarded)\n", pc - 4); }
        break;
    }
    case 0x32: {  // LWC2
        u32 addr = regs[rs] + imm;
        mxu.mtc2(rt, mem->read_u32(addr));
        break;
    }
    case 0x33: {  // LWC3
        mem->read_u32(regs[rs] + imm);
        static bool warned = false;
        if (!warned) { warned = true; printf("[CPU] LWC3 instruction at PC=0x%08X (load discarded)\n", pc - 4); }
        break;
    }
    case 0x34: {  // SC
        u32 addr = regs[rs] + imm;
        if (llbit && ll_addr == addr) {
            mem->write_u32(addr, regs[rt]);
            if (rt) regs[rt] = 1;
        } else {
            if (rt) regs[rt] = 0;
        }
        llbit = 0;
        break;
    }
    case 0x35: {  // SWC1 (nop - no store)
        static bool warned = false;
        if (!warned) { warned = true; printf("[CPU] SWC1 instruction at PC=0x%08X (store ignored)\n", pc - 4); }
        break;
    }
    case 0x36: {  // SWC2
        u32 addr = regs[rs] + imm;
        mem->write_u32(addr, mxu.mfc2(rt));
        break;
    }
    case 0x37: {  // SWC3 (nop - no store)
        static bool warned = false;
        if (!warned) { warned = true; printf("[CPU] SWC3 instruction at PC=0x%08X (store ignored)\n", pc - 4); }
        break;
    }

    default:
        printf("[CPU] Unknown opcode=0x%02X at PC=0x%08X insn=0x%08X\n", opcode, pc - 4, insn);
        raise_exception(EXC_RI);
        break;
    }
}

void CPU::trace_add(u32 pc_, u32 insn) {
    m_trace_pc[m_trace_idx] = pc_;
    m_trace_insn[m_trace_idx] = insn;
    m_trace_idx = (m_trace_idx + 1) % TRACE_SIZE;
}

void CPU::print_trace() {
    printf("\n=== Last %d instructions (most recent last) ===\n", TRACE_SIZE);
    for (int i = 0; i < TRACE_SIZE; i++) {
        int idx = (m_trace_idx - TRACE_SIZE + i + TRACE_SIZE) % TRACE_SIZE;
        if (m_trace_pc[idx] == 0) {
            printf("  -- skip zero at idx %d (buf was not full yet)\n", idx);
            continue;
        }
        printf("  [%03d] 0x%08X: 0x%08X  insn_count=%llu\n", i, m_trace_pc[idx], m_trace_insn[idx], 0ULL);
    }
}

void CPU::execute_one() {
    u32 insn = fetch();
    trace_add(pc, insn);
    u32 next_pc = pc + 4;
    pc = next_pc;

    // Sync to global for syscall access
    memcpy(g_cpu_regs, regs, sizeof(regs));
    g_cpu_pc = pc;
    g_cpu_hi = hi;
    g_cpu_lo = lo;

    execute(insn);

    // Likely-branch not taken: skip the delay-slot instruction entirely.
    // pc is still at next_pc (branch not taken), so advance past the delay slot.
    if (nullify_delay) {
        nullify_delay = false;
        pc = next_pc + 4;
        memcpy(g_cpu_regs, regs, sizeof(regs));
        g_cpu_pc = pc;
        g_cpu_hi = hi;
        g_cpu_lo = lo;
        cop0.tick();
        insn_count++;
        return;
    }

    // Detect JR/JALR to invalid address immediately
    if ((pc & 0x80000000) == 0 && pc >= 0x4000) {
        printf("[KUSEG] Immediate: pc=0x%08X from insn=0x%08X at 0x%08X\n",
               pc, insn, next_pc - 4);
        printf("[KUSEG] regs[31]=0x%08X regs[29]=0x%08X\n", regs[31], regs[29]);
        running = false;
        return;
    }

    // execute() modifies regs and pc directly; sync regs to global
    memcpy(g_cpu_regs, regs, sizeof(regs));
    g_cpu_hi = hi;
    g_cpu_lo = lo;

    // Handle delay slot
    if (pc != next_pc && pc != 0) {
        u32 branch_target = pc;
        u32 delay_pc = next_pc;

        u32 delay_insn = mem->read_u32(delay_pc);
        pc = delay_pc + 4;

        memcpy(g_cpu_regs, regs, sizeof(regs));
        g_cpu_pc = pc;
        g_cpu_hi = hi;
        g_cpu_lo = lo;
        execute(delay_insn);
        memcpy(g_cpu_regs, regs, sizeof(regs));
        g_cpu_hi = hi;
        g_cpu_lo = lo;

        // Check if delay slot set KUSEG
        if ((pc & 0x80000000) == 0 && pc >= 0x4000) {
            printf("[KUSEG] After delay slot: pc=0x%08X\n", pc);
            printf("[KUSEG] delay_insn=0x%08X branch_target=0x%08X\n",
                   delay_insn, branch_target);
            running = false;
            return;
        }

        pc = branch_target;
    }



    // GOT trampoline check
    if (mem->is_got_address(pc)) {
        int idx = mem->got_index(pc);
        if (idx >= 0 && (u32)idx < MAX_GOT_ENTRIES) {
            u32 return_addr = regs[31];
            syscalls->clear_task_switched();
            syscalls->dispatch(idx, return_addr);
            memcpy(regs, g_cpu_regs, sizeof(regs));
            hi = g_cpu_hi;
            lo = g_cpu_lo;
            // If the syscall did a task switch, g_cpu_pc holds the new task's resume PC.
            // Otherwise return to the caller via $ra (which dispatch may have set).
            pc = syscalls->task_switched() ? g_cpu_pc : return_addr;
            if ((pc & 0x80000000) == 0 && pc >= 0x4000) {
                printf("[KUSEG] GOT dispatch idx=%d pc=0x%08X (invalid)\n", idx, pc);
                printf("[KUSEG] return_addr=0x%08X\n", return_addr);
                running = false;
            }
        }
    }
    cop0.tick();  // increment Count register once per instruction
    insn_count++;
}

void CPU::run_until_pc(u32 stop_pc, u32 max_insns, u32 alt_stop_pc) {
    for (u32 i = 0; i < max_insns && running && pc != stop_pc; i++) {
        if (alt_stop_pc && pc == alt_stop_pc)
            break;
        execute_one();
    }
}

void CPU::do_vsync() {
    if (!syscalls) return;
    memcpy(g_cpu_regs, regs, sizeof(g_cpu_regs));
    g_cpu_pc = pc;
    g_cpu_hi = hi;
    g_cpu_lo = lo;

    syscalls->clear_task_switched();
    bool switched = syscalls->simulate_vsync();

    memcpy(regs, g_cpu_regs, sizeof(regs));
    hi = g_cpu_hi;
    lo = g_cpu_lo;
    if (switched) {
        pc = g_cpu_pc;
    }
}

void CPU::run_frame(u32 max_insns) {
    for (u32 i = 0; i < max_insns && running; i++) {
        execute_one();
    }
    do_vsync();
}
