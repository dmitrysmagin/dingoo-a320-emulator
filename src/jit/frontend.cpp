#include "frontend.h"

// Bit helpers (match cpu.cpp decode).
static inline u32 field_op(u32 w) { return (w >> 26) & 0x3F; }
static inline u32 field_rs(u32 w) { return (w >> 21) & 0x1F; }
static inline u32 field_rt(u32 w) { return (w >> 16) & 0x1F; }
static inline u32 field_rd(u32 w) { return (w >> 11) & 0x1F; }
static inline u32 field_sa(u32 w) { return (w >> 6) & 0x1F; }
static inline u32 field_func(u32 w) { return w & 0x3F; }
static inline s32 sext16(u32 w) { return (s32)(s16)(w & 0xFFFF); }

static inline u32 decode_j_target(u32 insn, u32 pc)
{
    // J-type: target = (imm26 << 2) | (pc & 0xF0000000)
    u32 imm26 = insn & 0x03FFFFFF;
    return (imm26 << 2) | (pc & 0xF0000000);
}

static inline u32 decode_branch_target(u32 insn, u32 pc)
{
    // I-type branches: target = pc + 4 + (sext16(imm16) << 2)
    s32 offset = sext16(insn & 0xFFFF);
    return (u32)((s32)pc + 4 + (offset << 2));
}

static JitOpProbe probe_special(u32 rs, u32 rt, u32 rd, u32 sa, u32 func) {
    JitOpProbe p;
    p.valid = false;
    p.stop = JIT_STOP_UNKNOWN;
    JitAluInsn o;
    o.rs = rs; o.rt = rt; o.rd = rd; o.sa = sa; o.imm = 0; o.uimm = 0;
    switch (func) {
    case 0x00: o.op = JIT_ALU_SLL; break;
    case 0x02: o.op = JIT_ALU_SRL; break;
    case 0x03: o.op = JIT_ALU_SRA; break;
    case 0x0A: o.op = JIT_ALU_MOVZ; break;
    case 0x0B: o.op = JIT_ALU_MOVN; break;
    case 0x0F: o.op = JIT_ALU_NOP; break;  // SYNC
    case 0x10: o.op = JIT_ALU_MFHI; break;
    case 0x11: o.op = JIT_ALU_MTHI; break;
    case 0x12: o.op = JIT_ALU_MFLO; break;
    case 0x13: o.op = JIT_ALU_MTLO; break;
    case 0x18: o.op = JIT_ALU_MULT; break;
    case 0x19: o.op = JIT_ALU_MULTU; break;
    case 0x1A: o.op = JIT_ALU_DIV; break;
    case 0x1B: o.op = JIT_ALU_DIVU; break;
    case 0x20: o.op = JIT_ALU_ADDU; break;  // ADD: trap disabled (MAME parity)
    case 0x21: o.op = JIT_ALU_ADDU; break;
    case 0x22: o.op = JIT_ALU_SUBU; break;  // SUB: trap disabled
    case 0x23: o.op = JIT_ALU_SUBU; break;
    case 0x24: o.op = JIT_ALU_AND; break;
    case 0x25: o.op = JIT_ALU_OR; break;
    case 0x26: o.op = JIT_ALU_XOR; break;
    case 0x27: o.op = JIT_ALU_NOR; break;
    case 0x2A: o.op = JIT_ALU_SLT; break;
    case 0x2B: o.op = JIT_ALU_SLTU; break;
    case 0x08: case 0x09:
        p.stop = JIT_STOP_JR; return p;     // JR/JALR: delay slot + GOT
    case 0x0C: case 0x0D:
        p.stop = JIT_STOP_TRAP; return p;   // SYSCALL/BREAK
    default:
        return p;  // SLLV/SRLV/SRAV + non-standard: not in Phase 1
    }
    if (rd == 0 && (func == 0x00 || func == 0x02 || func == 0x03 ||
                    func == 0x0A || func == 0x0B || func == 0x10 ||
                    func == 0x12 || (func >= 0x20 && func <= 0x2B)))
        o.op = JIT_ALU_NOP;
    p.valid = true;
    p.op = o;
    return p;
}
static JitOpProbe probe_special3(u32 insn, u32 rs, u32 rt, u32 rd, u32 func) {
    JitOpProbe p;
    p.valid = false;
    p.stop = JIT_STOP_SPECIAL3;
    // cpu.cpp exec_special3: func 0x00 = EXT, func 0x04 = INS.
    // pos = sa (bits 10:6), size = rd + 1 (rd encodes size-1).
    if (func == 0x00) {  // EXT
        JitAluInsn o;
        o.op = JIT_ALU_EXT;
        o.rs = rs; o.rt = rt; o.rd = rd;
        o.sa = 0;
        o.imm = (s32)insn;  // emitter re-decodes pos/msb
        o.uimm = 0;
        if (rt == 0)
            o.op = JIT_ALU_NOP;
        p.valid = true;
        p.op = o;
        return p;
    }
    if (func == 0x04) {  // INS
        JitAluInsn o;
        o.op = JIT_ALU_INS;
        o.rs = rs; o.rt = rt; o.rd = rd;
        o.sa = 0;
        o.imm = (s32)insn;
        o.uimm = 0;
        if (rt == 0)
            o.op = JIT_ALU_NOP;
        p.valid = true;
        p.op = o;
        return p;
    }
    return p;  // CLZ/CLO live in SPECIAL2 (op 0x1C), not here
}

static JitOpProbe probe_imm(u32 op, u32 rs, u32 rt, u32 insn) {
    JitOpProbe p;
    p.valid = false;
    p.stop = JIT_STOP_UNKNOWN;
    JitAluInsn o;
    o.rs = rs; o.rt = rt; o.rd = 0; o.sa = 0;
    o.imm = sext16(insn);
    o.uimm = insn & 0xFFFF;
    switch (op) {
    case 0x08: o.op = JIT_ALU_ADDI; break;
    case 0x09: o.op = JIT_ALU_ADDIU; break;
    case 0x0A: o.op = JIT_ALU_SLTI; break;
    case 0x0B: o.op = JIT_ALU_SLTIU; break;
    case 0x0C: o.op = JIT_ALU_ANDI; break;
    case 0x0D: o.op = JIT_ALU_ORI; break;
    case 0x0E: o.op = JIT_ALU_XORI; break;
    case 0x0F: o.op = JIT_ALU_LUI; break;
    default:
        return p;
    }
    if (rt == 0)
        o.op = JIT_ALU_NOP;
    p.valid = true;
    p.op = o;
    return p;
}

// Phase 3: fast-path loads/stores. Loads with rt==0 are NOP (interpreter
// guards `if (rt)`); stores ALWAYS execute (interpreter stores regs[0]==0).
static JitOpProbe probe_mem(u32 op, u32 rs, u32 rt, u32 insn) {
    JitOpProbe p;
    p.valid = true;
    p.stop = JIT_STOP_NONE;
    p.op.rs = rs; p.op.rt = rt; p.op.rd = 0;
    p.op.sa = 0; p.op.imm = sext16(insn); p.op.uimm = 0;
    bool is_store = (op == 0x28 || op == 0x29 || op == 0x2B);
    switch (op) {
    case 0x20: p.op.op = JIT_ALU_LB; break;
    case 0x21: p.op.op = JIT_ALU_LH; break;
    case 0x23: p.op.op = JIT_ALU_LW; break;
    case 0x24: p.op.op = JIT_ALU_LBU; break;
    case 0x25: p.op.op = JIT_ALU_LHU; break;
    case 0x28: p.op.op = JIT_ALU_SB; break;
    case 0x29: p.op.op = JIT_ALU_SH; break;
    default:   p.op.op = JIT_ALU_SW; break;
    }
    if (rt == 0 && !is_store)
        p.op.op = JIT_ALU_NOP;
    return p;
}

static bool is_slow_mem_op(u32 op) {
    // Everything memory-ish that the fast path does NOT compile: the
    // interpreter handles these (LL/SC semantics, LWL/R merge formulas,
    // CACHE/LWCx/SWCx nops, COP1X leftovers). Opcode numbers mirror cpu.cpp.
    switch (op) {
    case 0x1A: case 0x1B:
    case 0x22: case 0x26: case 0x27:  // LWL/LWR/SWL-ish unaligned merges
    case 0x2A: case 0x2E:  // SWL/SWR
    case 0x2F:  // CACHE
    case 0x30: case 0x31: case 0x32: case 0x33:
    case 0x34: case 0x35: case 0x36: case 0x37:
    case 0x38: case 0x39: case 0x3A: case 0x3B:
    case 0x3C: case 0x3D: case 0x3E: case 0x3F:
        return true;
    default:
        return false;
    }
}

JitOpProbe jit_probe_op(u32 insn) {
    u32 op = field_op(insn);
    u32 rs = field_rs(insn), rt = field_rt(insn), rd = field_rd(insn);
    u32 sa = field_sa(insn), func = field_func(insn);
    if (op == 0x00)
        return probe_special(rs, rt, rd, sa, func);
    if (op == 0x1C) {  // SPECIAL2
        // MUL (func 0x02) is MIPS-only (no MXU1 conflict per cpu.cpp:133).
        // MADD/MADDU/MSUB/MSUBU/CLZ/CLO conflict with MXU1 when MXU_EN is set
        // at RUNTIME (mxu.state.ctrl bit 0) — the JIT cannot know statically,
        // so they stay interpreter-only (Phase 4). Same for all MXU1 funcs.
        if (func == 0x02) {
            JitOpProbe p;
            p.valid = true;
            p.stop = JIT_STOP_NONE;
            p.op.op = JIT_ALU_MUL;
            p.op.rs = rs; p.op.rt = rt; p.op.rd = rd;
            p.op.sa = 0; p.op.imm = 0; p.op.uimm = 0;
            if (rd == 0)
                p.op.op = JIT_ALU_NOP;
            return p;
        }
        JitOpProbe p; p.valid = false; p.stop = JIT_STOP_SPECIAL2; return p;
    }
    if (op == 0x1F)
        return probe_special3(insn, rs, rt, rd, func);
    switch (op) {  // branches + jumps — Phase 2 (decode for inline delay slot)
    case 0x01: {  // REGIMM: BLTZ/BGEZ/BLTZAL/BGEZAL (+ likely variants)
        if (rt == 0 || rt == 1 || rt == 16 || rt == 17) {  // BLTZ, BGEZ, BLTZAL, BGEZAL
            JitOpProbe p; p.valid = true; p.stop = JIT_STOP_NONE;
            p.op.op = (rt == 0) ? JIT_ALU_BLTZ : (rt == 1) ? JIT_ALU_BGEZ :
                      (rt == 16) ? JIT_ALU_BLTZAL : JIT_ALU_BGEZAL;
            p.op.rs = rs; p.op.rt = rt; p.op.rd = 0;
            p.op.sa = 0; p.op.imm = sext16(insn); p.op.uimm = insn;
            return p;
        }
        // Other REGIMM ops are stop reasons
        JitOpProbe p; p.valid = false; p.stop = JIT_STOP_BRANCH; return p;
    }
    case 0x02: {  // J: unconditional jump
        JitOpProbe p; p.valid = true; p.stop = JIT_STOP_NONE;
        p.op.op = JIT_ALU_J; p.op.rs = 0; p.op.rt = 0; p.op.rd = 0;
        p.op.sa = 0; p.op.imm = (s32)decode_j_target(insn, 0); p.op.uimm = insn;
        return p;
    }
    case 0x03: {  // JAL: unconditional jump + link
        JitOpProbe p; p.valid = true; p.stop = JIT_STOP_NONE;
        p.op.op = JIT_ALU_JAL; p.op.rs = 0; p.op.rt = 0; p.op.rd = 0;
        p.op.sa = 0; p.op.imm = (s32)decode_j_target(insn, 0); p.op.uimm = insn;
        return p;
    }
    case 0x04: {  // BEQ
        JitOpProbe p; p.valid = true; p.stop = JIT_STOP_NONE;
        p.op.op = JIT_ALU_BEQ; p.op.rs = rs; p.op.rt = rt; p.op.rd = 0;
        p.op.sa = 0; p.op.imm = sext16(insn); p.op.uimm = insn;
        return p;
    }
    case 0x05: {  // BNE
        JitOpProbe p; p.valid = true; p.stop = JIT_STOP_NONE;
        p.op.op = JIT_ALU_BNE; p.op.rs = rs; p.op.rt = rt; p.op.rd = 0;
        p.op.sa = 0; p.op.imm = sext16(insn); p.op.uimm = insn;
        return p;
    }
    case 0x06: {  // BLEZ
        JitOpProbe p; p.valid = true; p.stop = JIT_STOP_NONE;
        p.op.op = JIT_ALU_BLEZ; p.op.rs = rs; p.op.rt = 0; p.op.rd = 0;
        p.op.sa = 0; p.op.imm = sext16(insn); p.op.uimm = insn;
        return p;
    }
    case 0x07: {  // BGTZ
        JitOpProbe p; p.valid = true; p.stop = JIT_STOP_NONE;
        p.op.op = JIT_ALU_BGTZ; p.op.rs = rs; p.op.rt = 0; p.op.rd = 0;
        p.op.sa = 0; p.op.imm = sext16(insn); p.op.uimm = insn;
        return p;
    }
    case 0x14: {  // BEQL (likely)
        JitOpProbe p; p.valid = true; p.stop = JIT_STOP_NONE;
        p.op.op = JIT_ALU_BEQL; p.op.rs = rs; p.op.rt = rt; p.op.rd = 0;
        p.op.sa = 0; p.op.imm = sext16(insn); p.op.uimm = insn;
        return p;
    }
    case 0x15: {  // BNEL (likely)
        JitOpProbe p; p.valid = true; p.stop = JIT_STOP_NONE;
        p.op.op = JIT_ALU_BNEL; p.op.rs = rs; p.op.rt = rt; p.op.rd = 0;
        p.op.sa = 0; p.op.imm = sext16(insn); p.op.uimm = insn;
        return p;
    }
    case 0x16: {  // BLEZL (likely)
        JitOpProbe p; p.valid = true; p.stop = JIT_STOP_NONE;
        p.op.op = JIT_ALU_BLEZL; p.op.rs = rs; p.op.rt = 0; p.op.rd = 0;
        p.op.sa = 0; p.op.imm = sext16(insn); p.op.uimm = insn;
        return p;
    }
    case 0x17: {  // BGTZL (likely)
        JitOpProbe p; p.valid = true; p.stop = JIT_STOP_NONE;
        p.op.op = JIT_ALU_BGTZL; p.op.rs = rs; p.op.rt = 0; p.op.rd = 0;
        p.op.sa = 0; p.op.imm = sext16(insn); p.op.uimm = insn;
        return p;
    }
    default:
        break;
    }
    if (op == 0x10 || op == 0x11 || op == 0x12 || op == 0x13) {
        JitOpProbe p; p.valid = false; p.stop = JIT_STOP_COP; return p;
    }
    // Phase 3: fast-path loads/stores compile inline; the rest exit to the
    // interpreter (which owns palette/GPIO/LCD/DMA/log/write-protect).
    if (op == 0x20 || op == 0x21 || op == 0x23 || op == 0x24 ||
        op == 0x25 || op == 0x28 || op == 0x29 || op == 0x2B)
        return probe_mem(op, rs, rt, insn);
    if (is_slow_mem_op(op)) {
        JitOpProbe p; p.valid = false; p.stop = JIT_STOP_MEM; return p;
    }
    return probe_imm(op, rs, rt, insn);
}

JitTbPlan jit_decode_tb(const u32* insns, u32 avail) {
    JitTbPlan plan;
    plan.count = 0;
    plan.stop = JIT_STOP_CAP;
    plan.stop_pc = 0;
    if (!insns || avail == 0)
        return plan;
    u32 n = (avail < JIT_TB_MAX_INSNS) ? avail : JIT_TB_MAX_INSNS;
    for (u32 i = 0; i < n; i++) {
        JitOpProbe pr = jit_probe_op(insns[i]);
        if (!pr.valid) {
            plan.stop = pr.stop;
            plan.stop_pc = i * 4;  // offset from TB start (caller adds base)
            return plan;
        }
        plan.ops[plan.count++] = pr.op;
    }
    plan.stop = JIT_STOP_CAP;
    plan.stop_pc = plan.count * 4;
    return plan;
}
