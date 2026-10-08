// Shared C++ reference semantics for JIT discharge tests (stub host builds).
// On JIT_HOST=x64 these live in x64/emit_{alu,mem,cop}.cpp instead.
#include "emit.h"

#include "../cop0.h"
#include "../mxu.h"

static void ext_fields(u32 insn, u32& pos, u32& size) {
    pos = (insn >> 6) & 0x1F;
    size = ((insn >> 11) & 0x1F) + 1;
}

static void ins_fields(u32 insn, u32& pos, u32& size) {
    pos = (insn >> 6) & 0x1F;
    size = ((insn >> 11) & 0x1F) + 1;
}

static u32 mask32(u32 size) {
    if (size >= 32) return 0xFFFFFFFFu;
    if (size == 0) return 0u;
    return (1u << size) - 1u;
}

static u32 ref_clz(u32 v) {
    if (v == 0) return 32;
    u32 c = 0;
    while ((v & 0x80000000u) == 0) { v <<= 1; c++; }
    return c;
}

void jit_apply_alu(const JitAluInsn& o, u32 regs[32], u32& hi, u32& lo) {
    u32 h = hi, l = lo;
    switch (o.op) {
        case JIT_ALU_NOP: break;
        case JIT_ALU_SLL: regs[o.rd] = regs[o.rt] << (o.sa & 0x1F); break;
        case JIT_ALU_SRL: regs[o.rd] = regs[o.rt] >> (o.sa & 0x1F); break;
        case JIT_ALU_SRA:
            regs[o.rd] = (u32)((s32)regs[o.rt] >> (o.sa & 0x1F));
            break;
        case JIT_ALU_ADDU: regs[o.rd] = regs[o.rs] + regs[o.rt]; break;
        case JIT_ALU_SUBU: regs[o.rd] = regs[o.rs] - regs[o.rt]; break;
        case JIT_ALU_AND: regs[o.rd] = regs[o.rs] & regs[o.rt]; break;
        case JIT_ALU_OR: regs[o.rd] = regs[o.rs] | regs[o.rt]; break;
        case JIT_ALU_XOR: regs[o.rd] = regs[o.rs] ^ regs[o.rt]; break;
        case JIT_ALU_NOR: regs[o.rd] = ~(regs[o.rs] | regs[o.rt]); break;
        case JIT_ALU_SLT:
            regs[o.rd] = (s32)regs[o.rs] < (s32)regs[o.rt] ? 1 : 0;
            break;
        case JIT_ALU_SLTU: regs[o.rd] = regs[o.rs] < regs[o.rt] ? 1 : 0; break;
        case JIT_ALU_MOVZ: if (regs[o.rt] == 0) regs[o.rd] = regs[o.rs]; break;
        case JIT_ALU_MOVN: if (regs[o.rt] != 0) regs[o.rd] = regs[o.rs]; break;
        case JIT_ALU_MFHI: regs[o.rd] = h; break;
        case JIT_ALU_MTHI: h = regs[o.rs]; break;
        case JIT_ALU_MFLO: regs[o.rd] = l; break;
        case JIT_ALU_MTLO: l = regs[o.rs]; break;
        case JIT_ALU_MULT: {
            s64 r = (s64)(s32)regs[o.rs] * (s64)(s32)regs[o.rt];
            l = (u32)r; h = (u32)((u64)r >> 32);
            break;
        }
        case JIT_ALU_MULTU: {
            u64 r = (u64)regs[o.rs] * (u64)regs[o.rt];
            l = (u32)r; h = (u32)(r >> 32);
            break;
        }
        case JIT_ALU_DIV:
            if (regs[o.rt] &&
                !(regs[o.rs] == 0x80000000u && regs[o.rt] == 0xFFFFFFFFu)) {
                l = (u32)((s32)regs[o.rs] / (s32)regs[o.rt]);
                h = (u32)((s32)regs[o.rs] % (s32)regs[o.rt]);
            }
            break;
        case JIT_ALU_DIVU:
            if (regs[o.rt]) {
                l = regs[o.rs] / regs[o.rt];
                h = regs[o.rs] % regs[o.rt];
            }
            break;
        case JIT_ALU_MUL: regs[o.rd] = regs[o.rs] * regs[o.rt]; break;
        case JIT_ALU_ADDI: regs[o.rt] = regs[o.rs] + (u32)o.imm; break;
        case JIT_ALU_ADDIU: regs[o.rt] = regs[o.rs] + (u32)o.imm; break;
        case JIT_ALU_SLTI:
            regs[o.rt] = (s32)regs[o.rs] < o.imm ? 1 : 0;
            break;
        case JIT_ALU_SLTIU: regs[o.rt] = regs[o.rs] < (u32)o.imm ? 1 : 0; break;
        case JIT_ALU_ANDI: regs[o.rt] = regs[o.rs] & o.uimm; break;
        case JIT_ALU_ORI: regs[o.rt] = regs[o.rs] | o.uimm; break;
        case JIT_ALU_XORI: regs[o.rt] = regs[o.rs] ^ o.uimm; break;
        case JIT_ALU_LUI: regs[o.rt] = o.uimm << 16; break;
        case JIT_ALU_EXT: {
            u32 pos, size;
            ext_fields((u32)o.imm, pos, size);
            u32 m = mask32(size);
            regs[o.rt] = (regs[o.rs] >> pos) & m;
            break;
        }
        case JIT_ALU_INS: {
            u32 pos, size;
            ins_fields((u32)o.imm, pos, size);
            u32 m = mask32(size);
            u32 field = (size >= 32) ? 0xFFFFFFFFu : (m << pos);
            regs[o.rt] = (regs[o.rt] & ~field) | ((regs[o.rs] & m) << pos);
            break;
        }
        case JIT_ALU_CLZ: regs[o.rd] = ref_clz(regs[o.rs]); break;
        case JIT_ALU_CLO: regs[o.rd] = ref_clz(~regs[o.rs]); break;
        default: break;
        }
    hi = h;
    lo = l;
}

void jit_run_reference(const JitTbPlan& plan, u32 regs[32], u32* hi, u32* lo) {
    u32 h = hi ? *hi : 0, l = lo ? *lo : 0;
    for (u32 i = 0; i < plan.count; i++)
        jit_apply_alu(plan.ops[i], regs, h, l);
    if (hi) *hi = h;
    if (lo) *lo = l;
}

static u32 mem_phys(u32 vaddr) {
    if ((vaddr & 0xC0000000u) == 0x80000000u)
        return vaddr & 0x1FFFFFFFu;
    return vaddr;
}

u32 jit_run_mem_reference(const JitTbPlan& plan, JitMemState& st, u32& fail_idx) {
    fail_idx = 0;
    for (u32 i = 0; i < plan.count; i++) {
        const JitAluInsn& o = plan.ops[i];
        if (st.tick_delta)
            (*st.tick_delta)++;
        switch (o.op) {
        case JIT_ALU_LB: case JIT_ALU_LH: case JIT_ALU_LW:
        case JIT_ALU_LBU: case JIT_ALU_LHU:
        case JIT_ALU_SB: case JIT_ALU_SH: case JIT_ALU_SW: {
            bool is_store = (o.op == JIT_ALU_SB || o.op == JIT_ALU_SH ||
                             o.op == JIT_ALU_SW);
            u32 size = (o.op == JIT_ALU_LB || o.op == JIT_ALU_LBU ||
                        o.op == JIT_ALU_SB) ? 1
                     : (o.op == JIT_ALU_LH || o.op == JIT_ALU_LHU ||
                        o.op == JIT_ALU_SH) ? 2 : 4;
            u32 vaddr = st.regs[o.rs] + (u32)o.imm;
            u32 phys = mem_phys(vaddr);
            bool slow = false;
            if (!st.ram || st.ram_size < size)
                slow = true;
            else if (phys > st.ram_size - size)
                slow = true;
            else if (is_store && phys >= st.code_start && phys < st.code_end)
                slow = true;
            if (slow) {
                if (st.tick_delta)
                    (*st.tick_delta)--;
                fail_idx = i;
                return (u32)JIT_EXIT_SLOW_MEM;
            }
            if (!is_store) {
                u32 v = 0;
                if (size == 1) v = st.ram[phys];
                else if (size == 2) v = (u32)st.ram[phys] | ((u32)st.ram[phys + 1] << 8);
                else v = (u32)st.ram[phys] | ((u32)st.ram[phys + 1] << 8) |
                         ((u32)st.ram[phys + 2] << 16) | ((u32)st.ram[phys + 3] << 24);
                if (o.op == JIT_ALU_LB) v = (u32)(s32)(s8)v;
                if (o.op == JIT_ALU_LH) v = (u32)(s32)(s16)v;
                st.regs[o.rt] = v;
            } else {
                u32 v = st.regs[o.rt];
                if (size == 1) st.ram[phys] = (u8)v;
                else if (size == 2) {
                    st.ram[phys] = (u8)v;
                    st.ram[phys + 1] = (u8)(v >> 8);
                } else {
                    st.ram[phys] = (u8)v;
                    st.ram[phys + 1] = (u8)(v >> 8);
                    st.ram[phys + 2] = (u8)(v >> 16);
                    st.ram[phys + 3] = (u8)(v >> 24);
                }
                if (st.wc && (phys >> 12) < st.wc_pages)
                    st.wc[phys >> 12]++;
            }
            break;
        }
        case JIT_COP_MFC0: case JIT_COP_MTC0:
        case JIT_COP_MFC2: case JIT_COP_MTC2:
        case JIT_COP_CFC2: case JIT_COP_CTC2:
        case JIT_COP_CUSTOM: case JIT_COP_MXU1:
            jit_apply_cop_one(o, st);
            break;
        default:
            jit_apply_alu(o, st.regs, st.hi, st.lo);
            break;
        }
    }
    return (u32)JIT_EXIT_DONE;
}

void jit_apply_cop_one(const JitAluInsn& o, JitMemState& st) {
    switch (o.op) {
    case JIT_COP_MFC0: st.regs[o.rt] = st.cop0->mfc0((int)o.rd); break;
    case JIT_COP_MTC0: st.cop0->mtc0((int)o.rd, st.regs[o.rt]); break;
    case JIT_COP_MFC2: st.regs[o.rt] = st.mxu->mfc2((int)o.rd); break;
    case JIT_COP_MTC2: st.mxu->mtc2((int)o.rd, st.regs[o.rt]); break;
    case JIT_COP_CFC2: st.regs[o.rt] = st.mxu->cfc2((int)o.rd); break;
    case JIT_COP_CTC2: st.mxu->ctc2((int)o.rd, st.regs[o.rt]); break;
    case JIT_COP_CUSTOM: st.mxu->exec_custom(o.uimm); break;
    case JIT_COP_MXU1: {
        u32 rt = (o.uimm >> 16) & 0x1F;
        u32 rd = (o.uimm >> 11) & 0x1F;
        if ((o.uimm & 0x3F) == 0x2E)
            st.regs[rt] = st.mxu->state.xregs[rd & 15];
        else
            st.mxu->state.xregs[rd & 15] = st.regs[rt];
        break;
    }
    default: break;
    }
}
