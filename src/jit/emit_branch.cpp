#include "emit.h"

#include <cstddef>

// Phase 6 branch-terminal emission: a TB ending in one static branch.
// The delay slot (always ops[branch_idx+1], validated ALU/COP by formation)
// is compiled inline; the TB exits with JIT_EXIT_NEXT_PC and st.next_pc
// set to the taken or fallthrough target. Mirrors cpu.cpp execute() +
// execute_one() order exactly:
//   - JAL/BLTZAL/BGEZAL write ra = branch_pc+8 BEFORE the delay slot.
//   - Conditions are evaluated BEFORE the delay slot (which may clobber
//     rs/rt), spilled on the stack across it (calls preserve RSP balance).
//   - Non-likely branches always run the delay slot; likely branches skip
//     it when not taken (mirrors nullify_delay).
// JR/JALR never reach here (frontend STOP_JR); mem delay slots are
// excluded by formation (a slow exit can't express a pending branch).

namespace {

void b8(JitEmit& e, u8 v) {
    if (e.len >= e.cap) { e.oom = true; return; }
    e.buf[e.len++] = v;
}

void b32(JitEmit& e, u32 v) {
    for (u32 i = 0; i < 4; i++) b8(e, (u8)(v >> (i * 8)));
}

void b_patch(JitEmit& e, u32 pos) {
    if (pos + 4 > e.cap) { e.oom = true; return; }
    u32 rel = e.len - (pos + 4);
    e.buf[pos] = (u8)rel; e.buf[pos + 1] = (u8)(rel >> 8);
    e.buf[pos + 2] = (u8)(rel >> 16); e.buf[pos + 3] = (u8)(rel >> 24);
}

u32 b_jcc(JitEmit& e, u8 cc) {
    b8(e, 0x0F); b8(e, cc);
    u32 p = e.len;
    b32(e, 0);
    return p;
}

// mov r32, [RDX+disp32], r = 0 EAX / 1 ECX.
void b_load(JitEmit& e, u32 r, u32 off) {
    b8(e, 0x8B);
    b8(e, (u8)(0x82 | (r << 3)));
    b32(e, off);
}

// Store next_pc then fall into the NEXT_PC exit.
void b_exit_next(JitEmit& e, u32 target) {
    emit_mov_rdx_disp32(e, JIT_OFF_NEXT_PC, target);
    jit_emit_epilog(e, (u32)JIT_EXIT_NEXT_PC);
}

bool is_likely(JitAluOp op) {
    return op == JIT_ALU_BEQL || op == JIT_ALU_BNEL ||
           op == JIT_ALU_BLEZL || op == JIT_ALU_BGTZL;
}

bool is_uncond(JitAluOp op) {
    return op == JIT_ALU_J || op == JIT_ALU_JAL;
}

bool needs_link(JitAluOp op) {
    return op == JIT_ALU_JAL || op == JIT_ALU_BLTZAL || op == JIT_ALU_BGEZAL;
}

// J-type target with the interpreter's exact high bits: cpu.cpp applies
// (pc & 0xF0000000) with pc already advanced past the branch (A+4).
u32 j_target(u32 insn_word, u32 branch_pc) {
    return ((insn_word & 0x03FFFFFFu) << 2) | ((branch_pc + 4) & 0xF0000000u);
}

// Emit condition into AL (0/1) for a conditional branch. Loads operands.
void b_emit_cond(JitEmit& e, const JitAluInsn& o) {
    switch (o.op) {
    case JIT_ALU_BEQ: case JIT_ALU_BEQL:
    case JIT_ALU_BNE: case JIT_ALU_BNEL:
        b_load(e, 0, slot_off(o.rs));
        b_load(e, 1, slot_off(o.rt));
        b8(e, 0x3B); b8(e, 0xC1);  // cmp eax, ecx
        b8(e, 0x0F);
        b8(e, (o.op == JIT_ALU_BEQ || o.op == JIT_ALU_BEQL) ? 0x94 : 0x95);
        b8(e, 0xC0);  // sete/setne al
        break;
    case JIT_ALU_BLEZ: case JIT_ALU_BLEZL:
        b_load(e, 0, slot_off(o.rs));
        b8(e, 0x83); b8(e, 0xF8); b8(e, 0x00);  // cmp eax, 0
        b8(e, 0x0F); b8(e, 0x9E); b8(e, 0xC0);  // setle al
        break;
    case JIT_ALU_BGTZ: case JIT_ALU_BGTZL:
        b_load(e, 0, slot_off(o.rs));
        b8(e, 0x83); b8(e, 0xF8); b8(e, 0x00);
        b8(e, 0x0F); b8(e, 0x9F); b8(e, 0xC0);  // setg al
        break;
    case JIT_ALU_BLTZ: case JIT_ALU_BLTZAL:
        b_load(e, 0, slot_off(o.rs));
        b8(e, 0x83); b8(e, 0xF8); b8(e, 0x00);
        b8(e, 0x0F); b8(e, 0x98); b8(e, 0xC0);  // sets al
        break;
    default:  // JIT_ALU_BGEZ / JIT_ALU_BGEZAL
        b_load(e, 0, slot_off(o.rs));
        b8(e, 0x83); b8(e, 0xF8); b8(e, 0x00);
        b8(e, 0x0F); b8(e, 0x99); b8(e, 0xC0);  // setns al
        break;
    }
    b8(e, 0x0F); b8(e, 0xB6); b8(e, 0xC0);  // movzx eax, al
}

// Inverse jump (skip delay + taken path) for likely branches.
u8 likely_skip_cc(JitAluOp op) {
    switch (op) {
    case JIT_ALU_BEQL: return 0x85;  // jne fallthrough
    case JIT_ALU_BNEL: return 0x84;  // je fallthrough
    case JIT_ALU_BLEZL: return 0x8F;  // jg fallthrough
    default: return 0x8E;  // BGTZL: jle fallthrough
    }
}

}  // namespace

u32 jit_compile_branch_tb(const JitTbPlan& plan, u32 branch_idx, u32 entry_pc,
                          u8* buf, u32 cap) {
    if (branch_idx + 2 > plan.count)
        return 0;
    const JitAluInsn& br = plan.ops[branch_idx];
    const JitAluInsn& delay = plan.ops[branch_idx + 1];
    if (!jit_op_is_branch(br.op))
        return 0;
    u32 branch_pc = entry_pc + branch_idx * 4;
    u32 fallthrough = branch_pc + 8;

    JitEmit e{buf, cap, 0, false};
    jit_emit_prolog(e);
    for (u32 i = 0; i < branch_idx; i++) {
        if (!jit_emit_op(e, plan.ops[i], i)) return 0;
        if (e.oom) return 0;
    }
    // Ticks: prefix ops each tick once; branch+delay share one tick
    // (mirrors execute_one ticking once per call). Emitted as one add.
    jit_emit_tick_add(e, branch_idx + 1);
    if (e.oom) return 0;

    if (is_uncond(br.op)) {
        if (needs_link(br.op))  // JAL only (J has no link)
            emit_mov_rdx_disp32(e, slot_off(31), branch_pc + 8);
        if (!jit_emit_op(e, delay, branch_idx + 1)) return 0;
        if (e.oom) return 0;
        b_exit_next(e, j_target(br.uimm, branch_pc));
        return e.oom ? 0 : e.len;
    }

    u32 target = (u32)((s32)branch_pc + 4 + (br.imm << 2));
    if (is_likely(br.op)) {
        // Evaluate; not-taken skips the delay slot (nullify_delay).
        b_emit_cond(e, br);
        b8(e, 0x85); b8(e, 0xC0);  // test eax, eax
        u32 jfall = b_jcc(e, likely_skip_cc(br.op));
        if (needs_link(br.op))
            emit_mov_rdx_disp32(e, slot_off(31), branch_pc + 8);
        if (!jit_emit_op(e, delay, branch_idx + 1)) return 0;
        if (e.oom) return 0;
        emit_mov_rdx_disp32(e, JIT_OFF_NEXT_PC, target);
        u32 jdone = emit_jmp32(e);
        b_patch(e, jfall);  // fallthrough:
        emit_mov_rdx_disp32(e, JIT_OFF_NEXT_PC, fallthrough);
        emit_patch32(e, jdone);  // done:
        jit_emit_epilog(e, (u32)JIT_EXIT_NEXT_PC);
        return e.oom ? 0 : e.len;
    }

    // Non-likely: condition first (delay may clobber rs/rt), spilled on the
    // stack across the delay slot (delay emission keeps RSP balanced), link
    // before delay per cpu.cpp order.
    b_emit_cond(e, br);
    b8(e, 0x50);  // push rax (cond 0/1)
    if (needs_link(br.op))
        emit_mov_rdx_disp32(e, slot_off(31), branch_pc + 8);
    if (!jit_emit_op(e, delay, branch_idx + 1)) return 0;
    if (e.oom) return 0;
    b8(e, 0x58);  // pop rax
    b8(e, 0x85); b8(e, 0xC0);  // test eax, eax
    u32 jfall = b_jcc(e, 0x84);  // jz fallthrough
    emit_mov_rdx_disp32(e, JIT_OFF_NEXT_PC, target);
    u32 jdone = emit_jmp32(e);
    b_patch(e, jfall);  // fallthrough:
    emit_mov_rdx_disp32(e, JIT_OFF_NEXT_PC, fallthrough);
    emit_patch32(e, jdone);  // done:
    jit_emit_epilog(e, (u32)JIT_EXIT_NEXT_PC);
    return e.oom ? 0 : e.len;
}

// ---- Phase 2 helpers (kept for compute_branch_target users/tests) ----

u32 compute_branch_target(const JitAluInsn& op, u32 pc)
{
    switch (op.op) {
        case JIT_ALU_J: // J: unconditional jump
        case JIT_ALU_JAL: // JAL: unconditional jump + link
            // J-type: target = (imm26 << 2) | (pc & 0xF0000000)
            return ((op.uimm << 2) & 0x0FFFFFFC) | (pc & 0xF0000000);
        case JIT_ALU_BEQ: case JIT_ALU_BNE:
        case JIT_ALU_BEQL: case JIT_ALU_BNEL:
        case JIT_ALU_BLEZ: case JIT_ALU_BGTZ:
        case JIT_ALU_BLEZL: case JIT_ALU_BGTZL:
            // I-type branches: target = pc + 4 + (sext16(imm16) << 2)
            return (u32)((s32)pc + 4 + (op.imm << 2));
        case JIT_ALU_BLTZ: case JIT_ALU_BGEZ:
        case JIT_ALU_BLTZAL: case JIT_ALU_BGEZAL:
            return (u32)((s32)pc + 4 + (op.imm << 2));
        default:
            return 0xFFFFFFFFu; // Invalid
    }
}

void emit_branch_exit(JitEmit& e, const JitAluInsn& op, u32 target)
{
    (void)op;
    // Store the target into next_pc (JitState.next_pc at JIT_OFF_NEXT_PC).
    emit_mov_rdx_disp32(e, JIT_OFF_NEXT_PC, target);
}
