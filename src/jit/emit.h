#ifndef JIT_EMIT_H
#define JIT_EMIT_H

// Phase 1 x86-64 emitter: straight-line ALU TBs, no third-party dependency.
//
// Hand-encoded emitter (REX/ModRM/SIB/disp8/disp32/imm32) targeting the
// JitState layout in jit.h: gpr[i] at byte offset i*4, hi/lo after gpr[32].
// The TB ABI is `u32 tb(JitState*)` with the pointer in RDI (SysV) or RCX
// (Win64). The emitter moves the live pointer into RDX once in the prolog,
// so the body is ABI-independent; all memory operands use RDX+disp32.
//
// Correctness contract (mirrors cpu.cpp):
//   - $0 reads as 0: loads from gpr[0] are skipped (slot is always 0).
//   - $0 writes dropped at decode (NOP) — emitter asserts rd/rt != 0.
//   - 32-bit wrap: ADD/ADDU/SUB/SUBU use 32-bit ops (no overflow trap).
//   - DIV/DIVU by zero: skip (no trap, no write) — matches interpreter.
//   - MULT/MULTU/MUL/DIV/DIVU HI/LO: 64-bit intermediate, exact C++ casts.
//   - Shifts mask the count to 5 bits (x86 HW does this natively).
//   - SRA uses arithmetic shift (SAR), SRL/SLL logical.
//   - SLT signed compare, SLTU unsigned; SLTI/SLTIU sign-extend the imm.
//   - ANDI/ORI/XORI zero-extend; LUI = imm<<16.
//   - EXT: rt=(rs>>pos)&((1<<size)-1), size=msb-lsb+1, size==32 -> no mask.
//   - INS: rt=(rt&~(mask<<pos))|((rs&mask)<<pos), size==32 -> full replace.
//   - MUL: rd=rs*rt low 32 bits (MIPS-only, no MXU conflict).
//   - MOVZ/MOVN: conditional move on rt==0 / rt!=0.
//   - MFHI/MFLO/MTHI/MTLO: hi/lo slots in JitState.
//   - Trap/NOP: SYNC and $0-dest ops emit nothing.
//
// Buffer model: caller provides a u8* buffer + capacity; emit_* appends and
// returns false on overflow. finalize() appends `mov eax, <exit>; ret`.
// compile_tb() decodes + emits a whole plan in one call.

#include "../types.h"
#include "frontend.h"
#include "jit.h"  // JitState layout + JIT_EXIT_*

// JitState byte offsets (checked with static_assert in emit_alu.cpp).
static constexpr u32 JIT_OFF_GPR = 0;            // gpr[i] at i*4
static constexpr u32 JIT_OFF_HI = 32 * 4;        // 128 - exit_code / HI
static constexpr u32 JIT_OFF_LO = 32 * 4 + 4;    // 132 - exit_arg / LO
static constexpr u32 JIT_OFF_NEXT_PC = 32 * 4 + 8; // 136 - Phase 2: branch target
static constexpr u32 JIT_OFF_PC = 32 * 4 + 12;     // 140 - Phase 2: current PC
static constexpr u32 JIT_OFF_HI_VAL = 32 * 4 + 16; // 144 - HI register value
static constexpr u32 JIT_OFF_LO_VAL = 32 * 4 + 20; // 148 - LO register value

// Emitter cursor over a raw byte buffer.
struct JitEmit {
    u8* buf;
    u32 cap;
    u32 len;
    bool oom;
    // Live base register holding JitState* after prolog (always EDX/RDX=2).
    // Fixed to 2 so no register allocation is needed in Phase 1.
};

// Compile one TB plan into buf (cap bytes). Returns emitted length, or 0 on
// overflow/unsupported op. Always appends the exit epilog on success.
u32 jit_compile_tb(const JitTbPlan& plan, u8* buf, u32 cap,
                   u32 exit_code = (u32)JIT_EXIT_DONE);

// Branch helpers for Phase 2: compute target, emit conditional/unconditional exit.
u32 compute_branch_target(const JitAluInsn& op, u32 pc);
void emit_branch_exit(JitEmit& e, const JitAluInsn& op, u32 target);

// Emitter helpers (used by both emit_alu and emit_branch)
void emit_mov_imm(JitEmit& e, u32 host_reg, u32 imm);
void emit_mov_rdx_disp32(JitEmit& e, u32 disp32, u32 imm);

// Discharge-test helper: run a plan against regs[32]+hi/lo without any
// executable memory. Implements the same semantics as the emitted code in
// plain C++ so tests can diff emitter-vs-reference on randomized inputs.
void jit_run_reference(const JitTbPlan& plan, u32 regs[32], u32* hi, u32* lo);

#endif // JIT_EMIT_H
