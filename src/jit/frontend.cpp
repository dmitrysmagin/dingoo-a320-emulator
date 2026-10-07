#include "frontend.h"

#include "../cpu.h"
#include "../syscalls.h"
#include "../memory.h"

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
// Phase 4: COP0/COP2 decode. Mirrors cpu.cpp execute() cases 0x10/0x12
// exactly, including quirks: MFC writes regs[rt] unconditionally (even
// rt==0), MTC reads regs[rt] (0 for $0), unknown rs in COP0 is a NOP.
// (WAIT is COP0/C0 func 0x20, which the interpreter traps via the C0
// default arm — so it decodes STOP_COP here, same as the other C0 traps.)
static JitOpProbe probe_cop0(u32 insn, u32 rt, u32 rd) {
    JitOpProbe p;
    p.valid = false;
    p.stop = JIT_STOP_COP;
    u32 rs = (insn >> 21) & 0x1F;
    u32 func = insn & 0x3F;
    if (rs == 0x00) {  // MFC0 (any func — interpreter ignores it)
        p.valid = true;
        p.stop = JIT_STOP_NONE;
        p.op.op = JIT_COP_MFC0;
        p.op.rs = 0; p.op.rt = rt; p.op.rd = rd;
        p.op.sa = 0; p.op.imm = 0; p.op.uimm = 0;
        return p;
    }
    if (rs == 0x04) {  // MTC0
        p.valid = true;
        p.stop = JIT_STOP_NONE;
        p.op.op = JIT_COP_MTC0;
        p.op.rs = 0; p.op.rt = rt; p.op.rd = rd;
        p.op.sa = 0; p.op.imm = 0; p.op.uimm = 0;
        return p;
    }
    if (rs == 0x10 && func == 0x18) {  // ERET -> TB exit (Phase 5: pc=epc)
        p.stop = JIT_STOP_ERET;
        return p;
    }
    if (rs == 0x02 || rs == 0x06) {
        // No such COP0 move exists; the interpreter's if/elif chain falls
        // through with no effect. Compile as NOP to keep TBs whole.
        p.valid = true;
        p.stop = JIT_STOP_NONE;
        p.op.op = JIT_ALU_NOP;
        p.op.rs = 0; p.op.rt = 0; p.op.rd = 0;
        p.op.sa = 0; p.op.imm = 0; p.op.uimm = 0;
        return p;
    }
    return p;  // C0 TLB ops / unknown funcs -> trap exit (STOP_COP)
}

static JitOpProbe probe_cop2(u32 insn, u32 rt, u32 rd) {
    JitOpProbe p;
    p.valid = true;
    p.stop = JIT_STOP_NONE;
    u32 rs = (insn >> 21) & 0x1F;
    p.op.rs = 0; p.op.rt = rt; p.op.rd = rd;
    p.op.sa = 0; p.op.imm = 0; p.op.uimm = insn;
    if (rs == 0x00) p.op.op = JIT_COP_MFC2;
    else if (rs == 0x04) p.op.op = JIT_COP_MTC2;
    else if (rs == 0x02) p.op.op = JIT_COP_CFC2;
    else if (rs == 0x06) p.op.op = JIT_COP_CTC2;
    else p.op.op = JIT_COP_CUSTOM;
    return p;
}

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
        // MADD/MADDU/MSUB/MSUBU/CLZ/CLO conflict with MXU_EN at RUNTIME
        // (mxu.state.ctrl bit 0) — the JIT cannot know statically, so they
        // stay interpreter-only. Same for all MXU1 funcs EXCEPT S32M2I
        // (0x2E) / S32I2M (0x2F), which cpu.cpp handles unconditionally
        // (plain GPR<->XR moves, no MXU_EN branch) and are safe via helper.
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
        if (func == 0x2E || func == 0x2F) {
            JitOpProbe p;
            p.valid = true;
            p.stop = JIT_STOP_NONE;
            p.op.op = JIT_COP_MXU1;
            p.op.rs = rs; p.op.rt = rt; p.op.rd = rd;
            p.op.sa = sa; p.op.imm = 0; p.op.uimm = insn;
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
    if (op == 0x10)
        return probe_cop0(insn, rt, rd);
    if (op == 0x12)
        return probe_cop2(insn, rt, rd);
    if (op == 0x11 || op == 0x13) {
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

bool jit_op_is_branch(JitAluOp op) {
    return op >= JIT_ALU_J && op <= JIT_ALU_BGTZL;
}

bool jit_op_is_mem(JitAluOp op) {
    switch (op) {
    case JIT_ALU_LB: case JIT_ALU_LH: case JIT_ALU_LW:
    case JIT_ALU_LBU: case JIT_ALU_LHU:
    case JIT_ALU_SB: case JIT_ALU_SH: case JIT_ALU_SW:
        return true;
    default:
        return false;
    }
}

bool jit_pc_eligible(Memory* mem, u32 pc) {
    if ((pc & 0x80000000u) == 0)
        return false;
    if (pc >= 0x80000000u && pc < 0x80A00000u)
        return false;
    if (mem->is_got_address(pc))
        return jit_form_got_at(mem, pc).n != 0;
    return mem->is_mapped(pc);
}

JitFormed jit_form_got_at(Memory* mem, u32 pc) {
    JitFormed f{};
    f.n = 0;
    f.has_branch = false;
    f.is_got = false;
    if (!mem->is_got_address(pc))
        return f;
    int idx0 = mem->got_index(pc);
    if (idx0 < 0)
        return f;
    for (; f.n < JIT_TB_MAX_INSNS && f.n < 2;) {
        u32 pn = pc + f.n * 4;
        if (mem->got_index(pn) != idx0)
            break;
        if (!mem->is_mapped(pn))
            break;
        u32 w = mem->read_u32(pn);
        JitOpProbe pr = jit_probe_op(w);
        if (!pr.valid || jit_op_is_branch(pr.op.op) || jit_op_is_mem(pr.op.op))
            break;
        f.words[f.n++] = w;
    }
    if (f.n == 0)
        return f;
    f.is_got = true;
    return f;
}

bool jit_got_entry_valid(CPU* cpu, u32 entry_pc, JitFormed* formed, int* got_idx) {
    if (!cpu || !cpu->mem || !cpu->syscalls || !formed || !got_idx)
        return false;
    JitFormed f = jit_form_got_at(cpu->mem, entry_pc);
    if (f.n == 0 || !f.is_got)
        return false;
    int idx = cpu->mem->got_index(entry_pc);
    if (idx < 0)
        return false;
    *got_idx = idx;
    *formed = f;
    return true;
}

JitFormed jit_form_tb(Memory* mem, u32 pc, u32 stop_pc, u32 alt_stop_pc) {
    JitFormed f;
    f.n = 0;
    f.has_branch = false;
    f.branch_idx = 0;
    f.is_got = false;
    for (; f.n < JIT_TB_MAX_INSNS;) {
        u32 pn = pc + f.n * 4;
        // Never overshoot a stop PC: the run loop must observe it.
        if (pn == stop_pc || (alt_stop_pc && pn == alt_stop_pc))
            break;
        if (!jit_pc_eligible(mem, pn))
            break;
        u32 w = mem->read_u32(pn);  // eligible => mapped RAM, no log spam
        JitOpProbe pr = jit_probe_op(w);
        if (!pr.valid)
            break;  // control op / stop for the interpreter
        if (!jit_op_is_branch(pr.op.op)) {
            f.words[f.n++] = w;
            continue;
        }
        // Terminal branch: needs its delay slot validated (valid,
        // non-branch, non-mem — mem slow-exits can't express a pending
        // branch, and nested branches mirror to the interpreter).
        u32 dp = pn + 4;
        if (dp == stop_pc || (alt_stop_pc && dp == alt_stop_pc))
            break;  // delay slot is the stop: run branch on interpreter
        if (!jit_pc_eligible(mem, dp))
            break;
        u32 dw = mem->read_u32(dp);
        JitOpProbe dpr = jit_probe_op(dw);
        if (!dpr.valid || jit_op_is_branch(dpr.op.op) || jit_op_is_mem(dpr.op.op))
            break;  // awkward delay slot: whole branch falls back
        if (f.n + 2 > JIT_TB_MAX_INSNS)
            break;
        f.words[f.n] = w;
        f.words[f.n + 1] = dw;
        f.has_branch = true;
        f.branch_idx = f.n;
        f.n += 2;
        break;
    }
    return f;
}
