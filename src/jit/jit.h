#ifndef JIT_H
#define JIT_H

#include <cstddef>
#include <unordered_map>
#include <vector>

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
#include "../cop0.h"
#include "../mxu.h"

// Shared CPU state for JIT-compiled translation blocks.
// Phase 0 uses only gpr[2] (v0), ra, and exit fields; later phases fill
// the rest (pc/next_pc/hi/lo/cop0/mxu/mem pointers per DYNAREC_PLAN.md).
struct JitState {
    u32 gpr[32];      // MIPS GPRs; gpr[31] = ra
    u32 exit_code;    // why the TB returned (see JitExit)
    u32 exit_arg;     // extra info (e.g. proof-TB marker)
    u32 next_pc;      // Phase 2: computed branch target for EXIT_NEXT_PC
    u32 pc;           // Phase 2: current PC (for delay slot)
    u32 hi;           // Phase 1: HI register
    u32 lo;           // Phase 1: LO register
    u64 mem_base;     // Phase 3: host pointer to Memory::m_mem[0] (0 = slow path)
    u32 mem_size;     // Phase 3: RAM size in bytes (bounds check)
    u32 code_start;   // Phase 3: code-section phys start (stores inside -> slow exit)
    u32 code_end;     // Phase 3: code-section phys end (exclusive)
    u64 wc_base;      // Phase 3: host pointer to write_counts[0] (u32 per 4K page)
    COP0* cop0;       // Phase 4: COP0 for MFC0/MTC0 helpers
    MXU* mxu;         // Phase 4: MXU for COP2 helpers
    u32 tick_delta;   // Phase 4: unflushed cop0.tick() count (Phase 5 flushes)
    u32 tick_pad;     // padding to keep 8-byte size
};

enum JitExit : u32 {
    JIT_EXIT_DONE = 0,   // proof TB executed v0=1 successfully
    JIT_EXIT_ERROR = 1,  // TB failed / not available
    JIT_EXIT_NEXT_PC = 2, // Phase 2: exit to dispatcher with next_pc set
    JIT_EXIT_SLOW_MEM = 3, // Phase 3: unmapped/MMIO/code access at op [exit_arg]
    JIT_EXIT_ERET = 4   // Phase 4: ERET executed (Phase 5: pc=epc, status&=~2)
};

enum JitMode {
    JIT_OFF = 0,  // pure interpreter (default, bit-identical to before)
    JIT_ON = 1,   // Phase 5: TB dispatcher (cached straight-line TBs + interp fallback)
};

struct CPU;  // cpu.h (jit.cpp includes it; header stays light)

struct JitStats {
    u64 tb_compiled = 0;  // TBs compiled into the exec pool
    u64 tb_hits = 0;      // cache hits (TB executed)
    u64 tb_misses = 0;    // cache misses that compiled OK
    u64 tb_uncompilable = 0;  // eligible PCs whose TB failed to compile
    u64 tb_branches = 0;  // compiled TBs ending in a static branch
    u64 tb_nextpc = 0;    // NEXT_PC exits taken (branches via TBs)
    u64 tb_insns = 0;     // guest insns executed via TBs
    u64 interp_insns = 0;  // guest insns executed via interpreter fallback
    u64 slow_exits = 0;   // TB slow-mem exits (faulting op ran on interpreter)
    u64 flushes = 0;      // cache flushes (code-gen change or LRU cap)
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

    // Phase 5 dispatcher: run guest code through cached straight-line TBs,
    // falling back to CPU::execute_one() for control flow, syscalls, GOT,
    // stops and unmapped PCs. Mirrors CPU::run_until_pc budget/stop
    // semantics (max_insns counts every guest insn, TB or fallback).
    // Safe to call with !is_ready() (pure-interpreter fallback).
    void run_until_pc(CPU* cpu, u32 stop_pc, u32 max_insns, u32 alt_stop_pc = 0);

    const JitStats& stats() const { return m_stats; }
    void print_stats() const;

    // Drop the whole TB cache + exec pools (phase transitions, which change
    // the active stop-PC set: a cached TB must never span a stop PC, and
    // formation only guards the stops passed at compile time).
    void invalidate_cache() { flush_locked(); }

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

    // Phase 5 TB cache: guest phys entry pc -> compiled TB. Flat mapping
    // means phys == strip(vaddr); KSEG0/KSEG1 aliases share entries.
    struct TbEntry {
        TbFunc func;
        u32 count;  // guest insns (pc advances count*4 on DONE)
    };
    struct Pool {
        void* p;
        u32 size;
        u32 used;
    };
    std::unordered_map<u32, TbEntry> m_cache;
    std::vector<Pool> m_pools;
    u32 m_cached_gen = 0;  // g_code_gen at last flush
    bool m_gen_valid = false;
    JitStats m_stats;
    JitState m_st;  // Phase 6 persistent slots (resident across TBs)

    static constexpr u32 kPoolSize = 1 << 20;  // 1 MB exec pools
    static constexpr u32 kMaxTbs = 4096;       // flush-all LRU cap

    void flush_locked();  // drop cache + pools, resync generation
    u8* pool_alloc(u32 len);
    // Try to compile a TB at pc (stops at stop PCs / ineligible pcs).
    // Returns true with func/count on success.
    bool compile_tb(CPU* cpu, u32 pc, u32 stop_pc, u32 alt_stop_pc,
                    TbFunc& func, u32& count);
};

#endif // JIT_H
