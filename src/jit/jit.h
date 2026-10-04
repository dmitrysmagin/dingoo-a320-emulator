#ifndef JIT_H
#define JIT_H

// Phase 0 JIT harness: lifecycle + dispatcher skeleton + stats.
//
// Phase 0 proves executable-memory allocation and the JIT<->interpreter
// boundary work on Windows (VirtualAlloc) and Linux (mmap) BEFORE any MIPS
// code generation exists. The "compiled TB" is a single hand-encoded x86-64
// function with signature `u32 tb_func(JitState*)` that emulates
//   ADDIU $v0, $zero, 1   (v0 = 1)
//   JR $ra                 (return to dispatcher)
// and returns EXIT_DONE. No asmjit dependency yet (added in Phase 1);
// the machine bytes are emitted directly so the OS exec-alloc path,
// W^X handling, and calling-convention shims are validated in isolation.
//
// Layout mirrors docs/DYNAREC_PLAN.md Phase 0:
//   main.cpp --jit flag -> Jit::init -> jit_enter() -> proof TB -> shutdown.

#include "../types.h"

// Shared CPU state for JIT-compiled translation blocks.
// Phase 0 uses only gpr[2] (v0), ra, and exit fields; later phases fill
// the rest (pc/next_pc/hi/lo/cop0/mxu/mem pointers per DYNAREC_PLAN.md).
struct JitState {
    u32 gpr[32];      // MIPS GPRs; gpr[31] = ra
    u32 exit_code;    // why the TB returned (see JitExit)
    u32 exit_arg;     // extra info (e.g. proof-TB marker)
};

enum JitExit : u32 {
    JIT_EXIT_DONE = 0,   // proof TB executed v0=1 successfully
    JIT_EXIT_ERROR = 1,  // TB failed / not available
};

enum JitMode {
    JIT_OFF = 0,  // pure interpreter (default, bit-identical to before)
    JIT_ON = 1,   // run Phase-0 proof TB once at startup, then interpret
};

class Jit {
public:
    Jit();
    ~Jit();

    // Non-copyable (owns executable pages).
    Jit(const Jit&) = delete;
    Jit& operator=(const Jit&) = delete;

    // Allocate executable page + emit proof TB. Returns false on failure
    // (caller falls back to interpreter; never fatal).
    bool init();

    // Release executable page. Safe to call without init().
    void shutdown();

    bool is_ready() const { return m_ready; }

    // Copy interpreter regs into JitState, run the proof TB, copy v0 back.
    // Returns the TB exit code, or JIT_EXIT_ERROR if not ready.
    // has_run_proof() reports whether the TB actually executed.
    u32 run_proof(u32 regs[32]);
    bool has_run_proof() const { return m_proof_runs > 0; }
    u64 proof_runs() const { return m_proof_runs; }
    u32 last_exit() const { return m_last_exit; }

    void print_stats() const;

private:
    // JIT function type: System V and Win64 both pass the single pointer
    // arg in (RDI / RCX) and return u32 in EAX — compatible for one arg.
    typedef u32 (*TbFunc)(JitState* state);

    // OS-specific executable allocation (VirtualAlloc / mmap).
    static void* exec_alloc(u32 size);
    static void exec_free(void* p, u32 size);

    // Hand-encoded proof TB bytes (see jit.cpp for disassembly).
    static const unsigned char kProofCode[];
    static const u32 kProofSize;

    void* m_page;
    u32 m_page_size;
    TbFunc m_proof_tb;
    bool m_ready;
    u64 m_proof_runs;
    u32 m_last_exit;
};

#endif // JIT_H
