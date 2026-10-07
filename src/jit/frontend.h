#ifndef JIT_FRONTEND_H
#define JIT_FRONTEND_H

// Phase 1 MIPS32 straight-line ALU frontend (extended through Phase 3).
//
// Decodes guest instructions (little-endian u32) into a TB plan: a bounded
// run of compilable ops terminated by a stop reason. The decoder's
// tick-box for each opcode mirrors CPU::execute()/exec_special() in
// cpu.cpp — anything the JIT does not understand ends the TB so the
// interpreter handles it (JR/JALR, SYSCALL/BREAK, COP0/COP2, slow mem,
// GOT targets are all stops, never compiled here).
// Phase 3 compiles LB/LH/LW/LBU/LHU/SB/SH/SW inline (fast RAM path;
// MMIO/unmapped/code-section exits to the interpreter).

#include "../types.h"

class Memory;  // memory.h (formation reads guest code; header stays light)

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
    JIT_STOP_ERET,       // ERET (Phase 5: exit with pc=epc, status&=~2)
};

// Compilable ALU op ids. One enum value per emitter case in emit_alu.cpp.
enum JitAluOp {
    // Phase 1: ALU ops
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

    // Phase 2: Branch + jump ops (delay slot compiled inline)
    JIT_ALU_J,       // J target
    JIT_ALU_JAL,     // JAL target
    JIT_ALU_JR,      // JR rs
    JIT_ALU_JALR,    // JALR rs (rd=ra implicit)
    JIT_ALU_BEQ,     // BEQ rs, rt, offset
    JIT_ALU_BNE,     // BNE rs, rt, offset
    JIT_ALU_BLEZ,    // BLEZ rs, offset
    JIT_ALU_BGTZ,    // BGTZ rs, offset
    JIT_ALU_BLTZ,    // BLTZ rs, offset (REGIMM rt=0)
    JIT_ALU_BGEZ,    // BGEZ rs, offset (REGIMM rt=1)
    JIT_ALU_BLTZAL,  // BLTZAL rs, offset (REGIMM rt=16)
    JIT_ALU_BGEZAL,  // BGEZAL rs, offset (REGIMM rt=17)
    JIT_ALU_BEQL,    // BEQL rs, rt, offset (likely not taken)
    JIT_ALU_BNEL,    // BNEL rs, rt, offset (likely not taken)
    JIT_ALU_BLEZL,   // BLEZL rs, offset (likely not taken)
    JIT_ALU_BGTZL,   // BGTZL rs, offset (likely not taken)
    
    // Phase 3: Memory ops (inline load/store)
    JIT_ALU_LB, JIT_ALU_LH, JIT_ALU_LW, JIT_ALU_LBU,
    JIT_ALU_LHU, JIT_ALU_SB, JIT_ALU_SH, JIT_ALU_SW,
    JIT_ALU_LWL, JIT_ALU_LWR, JIT_ALU_SWL, JIT_ALU_SWR,

    // Phase 4: COP0/COP2 ops (emitted as calls into cop0.cpp/mxu.cpp).
    // rt = GPR side, rd = coprocessor register (fs for COP2),
    // uimm = full insn word for CUSTOM/MXU1.
    JIT_COP_MFC0, JIT_COP_MTC0,
    JIT_COP_MFC2, JIT_COP_MTC2, JIT_COP_CFC2, JIT_COP_CTC2,
    JIT_COP_CUSTOM, JIT_COP_MXU1,
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

// Phase 6 op classifiers (over decoded ops).
// Branch = static control op J/JAL/cond/likely (JR/JALR never validate,
// so they never appear; the range includes them harmlessly).
bool jit_op_is_branch(JitAluOp op);
// Mem = fast-path load/store (the 8 inline ops; LWL-enum never decodes).
bool jit_op_is_mem(JitAluOp op);

// TB eligibility for a guest PC. Mirrors cpu.cpp fetch/GOT/halt rules so
// anything with special semantics falls back to CPU::execute_one():
// KUSEG/KSEG2/3 excluded (fetch-0/log + [KUSEG] halt), OS area excluded
// (fetch returns JR $ra), GOT excluded (dispatch + task switch).
bool jit_pc_eligible(Memory* mem, u32 pc);

// Phase 6 TB formation: a linear word run plus one optional terminal
// branch with a validated delay slot (valid, non-branch, non-mem).
// Never reads past stop PCs or ineligible PCs. Pure function of guest RAM.
struct JitFormed {
    u32 words[JIT_TB_MAX_INSNS];
    u32 n;            // words collected
    bool has_branch;  // words[n-2] is a branch, words[n-1] its delay slot
    u32 branch_idx;   // index of the branch word (delay slot at +1)
};
JitFormed jit_form_tb(Memory* mem, u32 pc, u32 stop_pc, u32 alt_stop_pc);

#endif // JIT_FRONTEND_H
