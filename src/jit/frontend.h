#ifndef JIT_FRONTEND_H
#define JIT_FRONTEND_H

// Phase 1 MIPS32 straight-line ALU frontend.
//
// Decodes guest instructions (little-endian u32) into a TB plan: a bounded
// run of compilable ALU ops terminated by a stop reason. The decoder's
// tick-box for each opcode mirrors CPU::execute()/exec_special() in
// cpu.cpp — anything the JIT does not understand ends the TB so the
// interpreter handles it (branches, loads/stores, COP0/COP2, JR/JALR,
// SYSCALL/BREAK, GOT targets are all stops, never compiled here).

#include "../types.h"

// Max guest insns per Phase-1 TB (matches DYNAREC_PLAN.md cap).
static constexpr u32 JIT_TB_MAX_INSNS = 64;

enum JitStop {
    JIT_STOP_NONE = 0,   // (unused) plan filled to cap
    JIT_STOP_CAP,        // hit JIT_TB_MAX_INSNS
    JIT_STOP_BRANCH,     // J/JAL/branch opcode (Phase 2)
    JIT_STOP_JR,         // JR/JALR (needs delay-slot + GOT check, Phase 2/5)
    JIT_STOP_MEM,        // load/store (Phase 3)
    JIT_STOP_COP,        // COP0/COP1/COP2/COP3 (Phase 4)
    JIT_STOP_SPECIAL2,   // SPECIAL2 incl. MXU + MADD family (Phase 4)
    JIT_STOP_SPECIAL3,   // SPECIAL3 other than EXT/INS (Phase 4)
    JIT_STOP_TRAP,       // SYSCALL/BREAK (exit to raise_exception, Phase 5)
    JIT_STOP_UNKNOWN,    // anything else the emitter cannot handle
};

// Compilable ALU op ids. One enum value per emitter case in emit_alu.cpp.
enum JitAluOp {
    JIT_ALU_SLL, JIT_ALU_SRL, JIT_ALU_SRA,
    JIT_ALU_ADDU, JIT_ALU_SUBU, JIT_ALU_AND, JIT_ALU_OR,
    JIT_ALU_XOR, JIT_ALU_NOR, JIT_ALU_SLT, JIT_ALU_SLTU,
    JIT_ALU_MOVZ, JIT_ALU_MOVN,
    JIT_ALU_MFHI, JIT_ALU_MTHI, JIT_ALU_MFLO, JIT_ALU_MTLO,
    JIT_ALU_MULT, JIT_ALU_MULTU, JIT_ALU_DIV, JIT_ALU_DIVU,
    JIT_ALU_ADDI, JIT_ALU_ADDIU, JIT_ALU_SLTI, JIT_ALU_SLTIU,
    JIT_ALU_ANDI, JIT_ALU_ORI, JIT_ALU_XORI, JIT_ALU_LUI,
    JIT_ALU_EXT, JIT_ALU_INS,
    JIT_ALU_MUL,
    JIT_ALU_CLZ, JIT_ALU_CLO,
    JIT_ALU_NOP,  // SYNC or rd==0-dropped op (emits nothing)
};

struct JitAluInsn {
    JitAluOp op;
    u32 rs, rt, rd;  // register indices (rd unused for immediates)
    u32 sa;          // shift amount (SLL/SRL/SRA) or EXT pos/size packing
    s32 imm;         // sign-extended immediate (ADDI/etc, EXT/INS fields)
    u32 uimm;        // zero-extended immediate (ANDI/ORI/XORI)
};

// A decoded TB plan: N ALU ops + the stop reason that ended it.
struct JitTbPlan {
    JitAluInsn ops[JIT_TB_MAX_INSNS];
    u32 count;
    JitStop stop;
    u32 stop_pc;  // guest PC of the stopping instruction
};

// Decode a straight-line run starting at `insns[0]` (up to `avail` words).
// Never reads past `avail`. Pure function, no emulator state.
JitTbPlan jit_decode_tb(const u32* insns, u32 avail);

// Single-op classifier used by tests: decode one word, report compilable op
// (valid=true) or the stop reason (valid=false).
struct JitOpProbe {
    bool valid;
    JitAluInsn op;   // meaningful iff valid
    JitStop stop;    // meaningful iff !valid
};
JitOpProbe jit_probe_op(u32 insn);

#endif // JIT_FRONTEND_H
