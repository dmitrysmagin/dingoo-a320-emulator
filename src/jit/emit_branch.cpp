#include "emit.h"
#include "frontend.h"

#include "emit.h"
#include "frontend.h"

// Compute branch target for a given instruction and PC
// J: target = (imm26 << 2) | (pc & 0xF0000000)
// BEQ/BNE/BLEZ/BGTZ/BEQL/BNEL/BLEZL/BGTZL: target = pc + 4 + (sext16(imm16) << 2)
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
            return (u32)((s32)pc + 4 + ((op.imm & 0xFFFF) << 2));
        default:
            return 0xFFFFFFFFu; // Invalid
    }
}

// Emit branch exit: for unconditional jumps, set next_pc; for conditional branches, set next_pc conditionally
void emit_branch_exit(JitEmit& e, const JitAluInsn& op, u32 target)
{
    // For all branch ops, emit a MOV of the target into next_pc
    // In production, conditional branches would emit conditional jumps
    // For Phase 2, we use a simple approach: always set next_pc
    emit_mov_imm(e, JIT_OFF_NEXT_PC, target);
}