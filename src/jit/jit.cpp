#include "jit.h"
#include "../cpu.h"
#include "frontend.h"
#include "emit.h"

#include <cstdio>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

// Hand-encoded x86-64 proof TB. Disassembly (Win64 shown; SysV uses RDI):
//
//   mov DWORD PTR [rcx + 8], 1   ; gpr[2] (v0) = 1  (offsetof(JitState,gpr[2]) == 8)
//   mov eax, 0                   ; return JIT_EXIT_DONE
//   ret
//
// The single JitState* arg arrives in RCX (Win64) or RDI (SysV); the TB
// stores through the ABI-correct register only. (The old dual-store wrote
// through garbage RDI on Windows — worked by luck until caller context
// changed, then segfaulted at startup.)
//
#ifdef _WIN32
const unsigned char Jit::kProofCode[] = {
    0xC7, 0x41, 0x08, 0x01, 0x00, 0x00, 0x00, // mov DWORD PTR [rcx+8], 1
    0x31, 0xC0,                               // xor eax, eax  (JIT_EXIT_DONE)
    0xC3,                                     // ret
};
#else
const unsigned char Jit::kProofCode[] = {
    0xC7, 0x47, 0x08, 0x01, 0x00, 0x00, 0x00, // mov DWORD PTR [rdi+8], 1
    0x31, 0xC0,                               // xor eax, eax  (JIT_EXIT_DONE)
    0xC3,                                     // ret
};
#endif
const u32 Jit::kProofSize = (u32)sizeof(Jit::kProofCode);

// JitState layout guard: gpr[2] must be at offset 8 for the encoding above.
struct JitStateLayoutCheck {
    char before[8];
    u32 v0_slot;
};
static_assert(sizeof(JitStateLayoutCheck().before) == 8, "gpr[2] offset changed");

Jit::Jit()
    : m_page(0)
    , m_page_size(0)
    , m_proof_tb(0)
    , m_ready(false)
    , m_proof_runs(0)
    , m_last_exit((u32)JIT_EXIT_ERROR) {
    memset(&m_st, 0, sizeof(m_st));
}

Jit::~Jit() {
    shutdown();
}

void* Jit::exec_alloc(u32 size) {
#ifdef _WIN32
    // MEM_COMMIT | MEM_RESERVE with PAGE_EXECUTE_READWRITE: single step is
    // enough for the Phase-0 proof. Phase 1+ switches to W^X (RW -> RX flip
    // after emit) via VirtualProtect.
    void* p = VirtualAlloc(0, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    return p;
#else
    // mmap RWX for Phase 0. Phase 1+ uses RW -> mprotect(RX) after emit.
    // MAP_ANONYMOUS is MAP_ANON on some BSDs; Linux/macOS accept MAP_ANONYMOUS.
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#ifdef MAP_JIT
    // Never set on Linux; harmless where defined (macOS ARM64 W^X path).
    (void)flags;
#endif
    void* p = mmap(0, size, PROT_READ | PROT_WRITE | PROT_EXEC, flags, -1, 0);
    return (p == MAP_FAILED) ? 0 : p;
#endif
}

void Jit::exec_free(void* p, u32 size) {
    if (!p)
        return;
#ifdef _WIN32
    (void)size;
    VirtualFree(p, 0, MEM_RELEASE);
#else
    munmap(p, size);
#endif
}

bool Jit::init() {
    if (m_ready)
        return true;
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    m_page_size = (u32)si.dwPageSize;
#else
    long ps = sysconf(_SC_PAGESIZE);
    m_page_size = (ps > 0) ? (u32)ps : 4096;
#endif
    if (m_page_size < kProofSize)
        m_page_size = 4096;

    m_page = exec_alloc(m_page_size);
    if (!m_page) {
        printf("[JIT] exec_alloc(%u) failed — staying on interpreter\n", m_page_size);
        return false;
    }
    memcpy(m_page, kProofCode, kProofSize);
#ifdef _WIN32
    // Ensure instruction cache coherence (mostly a no-op on x86-64, but correct).
    FlushInstructionCache(GetCurrentProcess(), m_page, kProofSize);
#else
    // x86-64 is cache-coherent for icache; keep the builtin for portability.
    __builtin___clear_cache((char*)m_page, (char*)m_page + kProofSize);
#endif
    m_proof_tb = (TbFunc)m_page;
    m_ready = true;
    printf("[JIT] Phase-0 harness ready: exec page %u bytes, proof TB %u bytes\n",
           m_page_size, kProofSize);
    return true;
}

void Jit::shutdown() {
    if (m_page) {
        exec_free(m_page, m_page_size);
        m_page = 0;
    }
    m_proof_tb = 0;
    for (auto& pl : m_pools)
        exec_free(pl.p, pl.size);
    m_pools.clear();
    m_cache.clear();
    m_gen_valid = false;
    m_ready = false;
}

u32 Jit::run_proof(u32 regs[32]) {
    if (!m_ready || !m_proof_tb) {
        m_last_exit = (u32)JIT_EXIT_ERROR;
        return m_last_exit;
    }
    JitState st;
    memset(&st, 0, sizeof(st));
    if (regs)
        memcpy(st.gpr, regs, sizeof(st.gpr));
    // ra (gpr[31]) is ignored by the proof TB, which returns directly.
    m_last_exit = m_proof_tb(&st);
    if (m_last_exit == (u32)JIT_EXIT_DONE && regs)
        regs[2] = st.gpr[2]; // v0 back to interpreter state
    m_proof_runs++;
    return m_last_exit;
}

void Jit::print_stats() const {
    printf("[JIT] ready=%d proof_runs=%llu last_exit=%u\n",
           m_ready ? 1 : 0, (unsigned long long)m_proof_runs, m_last_exit);
    u64 total_insns = m_stats.tb_insns + m_stats.interp_insns;
    printf("[JIT] tb_compiled=%llu hits=%llu misses-compiled=%llu uncompilable=%llu\n",
           (unsigned long long)m_stats.tb_compiled, (unsigned long long)m_stats.tb_hits,
           (unsigned long long)m_stats.tb_misses, (unsigned long long)m_stats.tb_uncompilable);
    printf("[JIT] branch_tbs=%llu nextpc_exits=%llu\n",
           (unsigned long long)m_stats.tb_branches, (unsigned long long)m_stats.tb_nextpc);
    printf("[JIT] tb_insns=%llu interp_insns=%llu slow_exits=%llu flushes=%llu cache=%u pools=%u\n",
           (unsigned long long)m_stats.tb_insns, (unsigned long long)m_stats.interp_insns,
           (unsigned long long)m_stats.slow_exits, (unsigned long long)m_stats.flushes,
           (u32)m_cache.size(), (u32)m_pools.size());
    if (total_insns)
        printf("[JIT] TB share: %.1f%% of guest insns\n",
               100.0 * (double)m_stats.tb_insns / (double)total_insns);
}

void Jit::flush_locked() {
    for (auto& pl : m_pools)
        exec_free(pl.p, pl.size);
    m_pools.clear();
    m_cache.clear();
    m_cached_gen = g_code_gen;
    m_gen_valid = true;
    m_stats.flushes++;
}

u8* Jit::pool_alloc(u32 len) {
    u32 need = (len + 15) & ~15u;
    for (auto& pl : m_pools) {
        if (pl.size - pl.used >= need) {
            u8* r = (u8*)pl.p + pl.used;
            pl.used += need;
            return r;
        }
    }
    void* p = exec_alloc(kPoolSize);
    if (!p)
        return nullptr;
    m_pools.push_back(Pool{p, kPoolSize, need});
    return (u8*)p;
}

bool Jit::compile_tb(CPU* cpu, u32 pc, u32 stop_pc, u32 alt_stop_pc,
                     TbFunc& func, u32& count) {
    // Phase 6 formation: linear run + optional terminal branch (with
    // validated delay slot). Shared with discharge tests (frontend.cpp).
    JitFormed formed = jit_form_tb(cpu->mem, pc, stop_pc, alt_stop_pc);
    if (formed.n == 0)
        return false;
    JitTbPlan plan = jit_decode_tb(formed.words, formed.n);
    // Decode is deterministic over validated words: a formed run decodes
    // 1:1 (branches validate by construction). Bail if it ever doesn't.
    if (plan.count != formed.n)
        return false;
    // Emit into a stack buffer first so a failed compile wastes no pool.
    u8 tmp[16 << 10];
    u32 len = 0;
    u32 equiv = plan.count;  // execute_one-equivalents (ticks + insn_count)
    if (formed.has_branch) {
        // Defensive: the branch must be where formation put it.
        if (formed.branch_idx + 2 > plan.count ||
            !jit_op_is_branch(plan.ops[formed.branch_idx].op))
            return false;
        len = jit_compile_branch_tb(plan, formed.branch_idx, pc, tmp, (u32)sizeof(tmp));
        // Branch+delay share one tick/insn_count with the interpreter
        // (execute_one ticks once for both): prefix + 1.
        equiv = formed.branch_idx + 1;
    } else {
        len = jit_compile_tb(plan, tmp, (u32)sizeof(tmp), (u32)JIT_EXIT_DONE);
    }
    if (!len) {
        m_stats.tb_uncompilable++;
        return false;
    }
    if (m_cache.size() >= kMaxTbs)
        flush_locked();  // LRU cap: simple flush-all (rare, always correct)
    u8* dst = pool_alloc(len);
    if (!dst)
        return false;  // exec OOM: interpreter covers (never fatal)
    memcpy(dst, tmp, len);
#ifdef _WIN32
    FlushInstructionCache(GetCurrentProcess(), dst, len);
#else
    __builtin___clear_cache((char*)dst, (char*)dst + len);
#endif
    // Phase 6: key by entry vaddr (not phys). J-targets embed
    // (pc & 0xF0000000), so KSEG0/KSEG1 aliases need separate TBs.
    m_cache[pc] = TbEntry{(TbFunc)dst, equiv};
    m_stats.tb_compiled++;
    m_stats.tb_misses++;
    if (formed.has_branch)
        m_stats.tb_branches++;
    func = (TbFunc)dst;
    count = equiv;
    return true;
}

void Jit::run_until_pc(CPU* cpu, u32 stop_pc, u32 max_insns, u32 alt_stop_pc) {
    if (!m_ready || !cpu) {
        if (cpu)
            cpu->run_until_pc(stop_pc, max_insns, alt_stop_pc);
        return;
    }
    Memory* mem = cpu->mem;
    // Phase 6 persistent state: slots stay resident across TBs (no per-TB
    // memcpy). Synced cpu->st once here, st->cpu around each fallback and
    // once at exit. Sticky pointers are set once per call (mappings are
    // stable; generation flushes cover remaps).
    JitState& st = m_st;
    memcpy(st.gpr, cpu->regs, sizeof(st.gpr));
    st.hi = cpu->hi;
    st.lo = cpu->lo;
    st.mem_base = (u64)(uintptr_t)mem->get_raw_ptr();
    st.mem_size = mem->size();
    st.code_start = mem->code_start();
    st.code_end = mem->code_end();
    st.wc_base = (u64)(uintptr_t)mem->write_counts_base();
    st.cop0 = &cpu->cop0;
    st.mxu = &cpu->mxu;
    u32 executed = 0;
    while (cpu->running && cpu->pc != stop_pc && executed < max_insns) {
        if (alt_stop_pc && cpu->pc == alt_stop_pc)
            break;
        // Code remap (dl_load/dl_free/dl_res map+close) drops the cache.
        // Checked per iteration: remaps happen inside fallback syscalls.
        if (!m_gen_valid || g_code_gen != m_cached_gen)
            flush_locked();
        u32 pc = cpu->pc;
        TbFunc func = nullptr;
        u32 count = 0;
        if (jit_pc_eligible(mem, pc)) {
            auto it = m_cache.find(pc);  // vaddr-keyed (J-target aliasing)
            if (it != m_cache.end()) {
                func = it->second.func;
                count = it->second.count;
                m_stats.tb_hits++;
            } else if (!compile_tb(cpu, pc, stop_pc, alt_stop_pc, func, count)) {
                func = nullptr;
            }
        }
        if (!func) {
            // JR/JALR, stops, GOT, unmapped, awkward delay slots: the
            // interpreter owns it. Sync state around the call.
            memcpy(cpu->regs, st.gpr, sizeof(st.gpr));
            cpu->hi = st.hi;
            cpu->lo = st.lo;
            cpu->execute_one();
            memcpy(st.gpr, cpu->regs, sizeof(st.gpr));
            st.hi = cpu->hi;
            st.lo = cpu->lo;
            executed++;
            m_stats.interp_insns++;
            continue;
        }
        // Only the tick counter is per-TB (everything else is resident).
        st.tick_delta = 0;
        u32 exit = func(&st);
        if (exit == (u32)JIT_EXIT_SLOW_MEM) {
            // Prefix applied exit_arg ops (branch, if any, never reached).
            u32 applied = st.exit_arg;
            cpu->pc = pc + applied * 4;
            m_stats.slow_exits++;
            // Faulting op (MMIO/unmapped/code) via the interpreter, which
            // owns palette/GPIO/log semantics plus its own tick + count.
            memcpy(cpu->regs, st.gpr, sizeof(st.gpr));
            cpu->hi = st.hi;
            cpu->lo = st.lo;
            for (u32 i = 0; i < st.tick_delta; i++)
                cpu->cop0.tick();
            cpu->insn_count += applied;
            executed += applied;
            m_stats.tb_insns += applied;
            cpu->execute_one();
            memcpy(st.gpr, cpu->regs, sizeof(st.gpr));
            st.hi = cpu->hi;
            st.lo = cpu->lo;
            executed++;
            m_stats.interp_insns++;
        } else if (exit == (u32)JIT_EXIT_DONE) {
            cpu->pc = pc + count * 4;
            for (u32 i = 0; i < st.tick_delta; i++)
                cpu->cop0.tick();
            cpu->insn_count += count;
            executed += count;
            m_stats.tb_insns += count;
        } else if (exit == (u32)JIT_EXIT_NEXT_PC) {
            // Branch TB: st.next_pc holds taken/fallthrough target.
            cpu->pc = st.next_pc;
            m_stats.tb_nextpc++;
            for (u32 i = 0; i < st.tick_delta; i++)
                cpu->cop0.tick();
            cpu->insn_count += count;
            executed += count;
            m_stats.tb_insns += count;
        } else {
            printf("[JIT] unexpected exit=%u at PC=0x%08X — halting\n", exit, pc);
            memcpy(cpu->regs, st.gpr, sizeof(st.gpr));
            cpu->hi = st.hi;
            cpu->lo = st.lo;
            cpu->running = false;
        }
        // NOTE: max_insns may overshoot by <1 TB (bounded, documented).
    }
    // Slots are live: write back so do_vsync/main see current state.
    memcpy(cpu->regs, st.gpr, sizeof(st.gpr));
    cpu->hi = st.hi;
    cpu->lo = st.lo;
}
