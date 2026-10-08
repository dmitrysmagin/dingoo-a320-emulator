#ifndef JIT_EMIT_H
#define JIT_EMIT_H

// JIT host backend: IA-32 (JIT_HOST=x86). Same plan as x64/, 32-bit encodings.
//
// Hand-encoded emitter (ModRM/SIB/disp32/imm32) targeting JitState in jit.h.
// TB ABI: `u32 __cdecl tb(JitState*)` — arg at [esp+4] on entry. Prolog loads
// ESI once as the state base (callee-saved; EDX is clobbered by mul/div/mem).
// Body uses [ESI+disp32] like x64 uses RDX.
// Build the emulator with -m32 so emitted code matches the host pointer size.
//
// Correctness contract (mirrors cpu.cpp):
//   - $0 reads as 0: loads from gpr[0] are skipped (slot is always 0).
//   - $0 writes dropped at decode (NOP) — emitter asserts rd/rt != 0.
//   - 32-bit wrap: ADD/ADDU/SUB/SUBU use 32-bit ops (no overflow trap).
//   - DIV/DIVU by zero: skip (no trap, no write) — matches interpreter.
//     DIV INT_MIN/-1 also skips (x86 IDIV would #DE; interpreter has UB).
//   - MULT/MULTU/MUL/DIV/DIVU HI/LO: 64-bit intermediate, exact C++ casts.
//   - Shifts mask the count to 5 bits (x86 HW does this natively).
//   - SRA uses arithmetic shift (SAR), SRL/SLL logical.
//   - SLT signed compare, SLTU unsigned; SLTI signed vs sign-extended imm,
//     SLTIU unsigned vs sign-extended imm as u32 (matches cpu.cpp).
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

#include "../../types.h"
#include "../../cop0.h"
#include "../../mxu.h"
#include "../frontend.h"
#include "../jit.h"  // JitState layout + JIT_EXIT_*

#include <cstddef>

// JitState offsets follow the host pointer width (-m32 vs -m64).
static constexpr u32 JIT_OFF_GPR = 0;
static constexpr u32 JIT_OFF_HI = (u32)offsetof(JitState, exit_code);
static constexpr u32 JIT_OFF_LO = (u32)offsetof(JitState, exit_arg);
static constexpr u32 JIT_OFF_NEXT_PC = (u32)offsetof(JitState, next_pc);
static constexpr u32 JIT_OFF_PC = (u32)offsetof(JitState, pc);
static constexpr u32 JIT_OFF_HI_VAL = (u32)offsetof(JitState, hi);
static constexpr u32 JIT_OFF_LO_VAL = (u32)offsetof(JitState, lo);
static constexpr u32 JIT_OFF_MEM_BASE = (u32)offsetof(JitState, mem_base);
static constexpr u32 JIT_OFF_MEM_SIZE = (u32)offsetof(JitState, mem_size);
static constexpr u32 JIT_OFF_EXIT_ARG = (u32)offsetof(JitState, exit_arg);
static constexpr u32 JIT_OFF_CODE_START = (u32)offsetof(JitState, code_start);
static constexpr u32 JIT_OFF_CODE_END = (u32)offsetof(JitState, code_end);
static constexpr u32 JIT_OFF_WC_BASE = (u32)offsetof(JitState, wc_base);
static constexpr u32 JIT_OFF_COP0_PTR = (u32)offsetof(JitState, cop0);
static constexpr u32 JIT_OFF_MXU_PTR = (u32)offsetof(JitState, mxu);
static constexpr u32 JIT_OFF_SYSCALLS_PTR = (u32)offsetof(JitState, syscalls);
static constexpr u32 JIT_OFF_TICK_DELTA = (u32)offsetof(JitState, tick_delta);
static constexpr u32 JIT_OFF_INSN_DELTA = (u32)offsetof(JitState, insn_delta);
static constexpr u32 JIT_PROLOG_CHAIN_OFF = 5; // skip push esi; mov esi,[esp+8] on chain entry

// Byte offset of a chain exit site within a TB (for patching).
static constexpr u32 JIT_CHAIN_SITE_SIZE = 17;  // mov [esi+disp32],imm (10) + mov eax,imm; pop esi; ret (7)

// Emitter cursor over a raw byte buffer.
struct JitEmit {
    u8* buf;
    u32 cap;
    u32 len;
    bool oom;
    // Live base register holding JitState* after prolog (host reg 6 = ESI).
    // Fixed to 2 so no register allocation is needed in Phase 1.
};

// Compile one TB plan into buf (cap bytes). Returns emitted length, or 0 on
// overflow/unsupported op. Always appends the exit epilog on success.

// Branch helpers for Phase 2: compute target, emit conditional/unconditional exit.
u32 compute_branch_target(const JitAluInsn& op, u32 pc);
void emit_branch_exit(JitEmit& e, const JitAluInsn& op, u32 target);

// Phase 6: compile a branch-ended TB — prefix ops[0, branch_idx) via
// jit_emit_op, then the terminal branch ops[branch_idx] with its delay
// slot ops[branch_idx+1], ending in a NEXT_PC exit (st.next_pc = target).
// entry_pc is the TB's guest address (link/target/fallthrough derive from
// it, matching cpu.cpp execute/execute_one order: link before delay,
// condition evaluated before delay, likely-not-taken skips delay).
// Returns emitted length, or 0 on overflow/unsupported op.


// Emitter helpers (used by both emit_alu and emit_branch)
void emit_mov_imm(JitEmit& e, u32 host_reg, u32 imm);
void emit_mov_rdx_disp32(JitEmit& e, u32 disp32, u32 imm);

// Phase 6 shared raw-output primitives (thin wrappers over emit_alu
// statics, so branch/mem emitters need no duplication).
void jit_emit_prolog(JitEmit& e, u32 entry_pc, u32 insn_count);  // state* -> EDX
void jit_emit_epilog(JitEmit& e, u32 exit_code);  // mov eax, exit; ret
void jit_emit_tick_add(JitEmit& e, u32 count);    // tick_delta += count
u32 emit_jcc32(JitEmit& e, u8 cc);             // 0F cc + rel32 placeholder
u32 emit_jmp32(JitEmit& e);                    // E9 + rel32 placeholder
void emit_patch32(JitEmit& e, u32 pos);        // patch placeholder to here
// Emit one already-validated op (ALU/COP; mem allowed with op_idx for the
// slow-exit arg). Used for TB prefixes and delay slots. Returns false on
// overflow/unsupported op.
bool jit_emit_op(JitEmit& e, const JitAluInsn& op, u32 op_idx);

// Phase 3: emit one fast-path load/store (LB/LH/LW/LBU/LHU/SB/SH/SW).
// Slow cases (unmapped/MMIO/code-section/null base) exit the TB with
// JIT_EXIT_SLOW_MEM and exit_arg = op_idx so the dispatcher can resume
// the interpreter at the right PC. Returns false on overflow.
bool emit_mem_op(JitEmit& e, const JitAluInsn& op, u32 op_idx);

// Phase 6c: chain exit helpers.
// Emit a 16-byte patchable exit site (see emit_alu.cpp for layouts).
// When g_jit_chain_stub is set: mov next_pc + jmp stub (miss resolves in stub).
// Otherwise (discharge tests): mov next_pc + mov eax,NEXT_PC + ret.
u32 jit_emit_chain_exit(JitEmit& e, u32 target_pc, JitChainInfo* info);
// Patch site to direct chain entry (TB + JIT_PROLOG_CHAIN_OFF).
void jit_patch_chain_site(u8* tb_base, u32 code_off, u8* chain_entry);
// Wire the jmp at tb_base+code_off+10 to the shared miss stub (rel32).
void jit_patch_chain_site_jmp_stub(u8* tb_base, u32 code_off, u8* stub);
// Non-null after Jit::init(): branch TBs jmp here when the target is uncached.
extern u8* g_jit_chain_stub;

// Phase 6: compile TB plans into raw output buffers.
// entry_pc is the MIPS PC of the first instruction in this TB.
u32 jit_compile_tb(const JitTbPlan& plan, u8* buf, u32 cap, u32 exit_code, u32 entry_pc);
// branch_idx indexes the branch insn in plan.ops; the delay slot
// and any prefix ops are emitted as part of this TB.
u32 jit_compile_branch_tb(const JitTbPlan& plan, u32 branch_idx, u32 entry_pc, u8* buf, u32 cap, JitChainInfo* chain = nullptr);

// Phase 6c: GOT dispatch TB — jit_got_dispatch(st, got_index) only (stub
// words in the slot are not executed; matches cpu.cpp JAL/JR landing).
u32 jit_compile_got_tb(u32 entry_pc, s32 got_index, u8* buf, u32 cap);
extern "C" u32 jit_got_dispatch(JitState* st, s32 got_index);

// Per-op ALU semantics shared by jit_run_reference() and the mem reference
// below (single source of truth for discharge diffs).
void jit_apply_alu(const JitAluInsn& o, u32 regs[32], u32& hi, u32& lo);

// Discharge-test helper: run a plan against regs[32]+hi/lo without any
// executable memory. Implements the same semantics as the emitted code in
// plain C++ so tests can diff emitter-vs-reference on randomized inputs.
void jit_run_reference(const JitTbPlan& plan, u32 regs[32], u32* hi, u32* lo);

// Phase 3 memory reference: applies load/store ops in `plan` to a flat RAM
// buffer exactly like the emitted fast path (KSEG strip, bounds check,
// code-section reject on stores, write_counts bump). Returns the exit code
// the TB would produce (JIT_EXIT_DONE or JIT_EXIT_SLOW_MEM); on slow,
// `fail_idx` is the op index (== exit_arg) and later ops are unapplied.
struct JitMemState {
    u32 regs[32];
    u32 hi, lo;
    u8* ram;          // host RAM base (== JitState.mem_base)
    u32 ram_size;     // RAM size in bytes (== JitState.mem_size)
    u32 code_start;   // code-section phys range (stores inside -> slow)
    u32 code_end;
    u32* wc;          // write_counts (u32 per 4K page), may be null
    u32 wc_pages;     // wc entries available
    COP0* cop0;       // Phase 4: COP0 for MFC0/MTC0 (null = no COP ops)
    MXU* mxu;         // Phase 4: MXU for COP2 (null = no COP ops)
    u32* tick_delta;  // Phase 4: bumped once per executed op (may be null)
};
u32 jit_run_mem_reference(const JitTbPlan& plan, JitMemState& st, u32& fail_idx);

// Phase 4: emit one COP op (MFC0/MTC0/MFC2/MTC2/CFC2/CTC2/custom/MXU1) as a
// call into the existing C++ implementations (correctness, not speed).
// The call preserves the RDX state base and follows the host ABI (Win64
// shadow space / SysV). Returns false on overflow/unsupported op.
bool emit_cop_op(JitEmit& e, const JitAluInsn& op);

// Phase 4: apply one COP op to regs + COP0/MXU objects (reference side of
// the discharge diff; mirrors the helpers in emit_cop.cpp call for call).
void jit_apply_cop_one(const JitAluInsn& o, JitMemState& st);

// Offset of a GPR slot within JitState (gpr[i] at offset i*4).
static inline u32 slot_off(u32 reg) { return reg * 4; }

#endif // JIT_EMIT_H
