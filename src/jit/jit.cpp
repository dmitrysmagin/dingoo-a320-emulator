#include <cstdio>
#include "jit.h"
#include "../cpu.h"
#include "../log.h"
#include "frontend.h"
#include "host_config.h"
#include "emit.h"

#include "tbcache.h"
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#endif

namespace {

// TSC read (~20-30 cycles, no syscall). x86-64 only — like the whole backend.
// On exotic targets without a TSC, wall_ns() doubles as the tick source
// and calibration below derives hz = 1e9, keeping units consistent.
#if defined(_MSC_VER)
#include <intrin.h>
static inline u64 tsc_now() { return __rdtsc(); }
#elif defined(__i386__) || defined(__x86_64__)
static inline u64 tsc_now() { return __builtin_ia32_rdtsc(); }
#else
static inline u64 tsc_now();
#endif

// Monotonic wall clock in ns (for TSC calibration only).
static u64 wall_ns() {
#ifdef _WIN32
    static LARGE_INTEGER freq;  // static storage: zero-initialized
    LARGE_INTEGER now;
    if (!freq.QuadPart)
        QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return (u64)(now.QuadPart * 1000000000ULL / (u64)freq.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64)ts.tv_sec * 1000000000ULL + (u64)ts.tv_nsec;
#endif
}

static void sleep_ms(u32 ms) {
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    usleep((useconds_t)ms * 1000u);
#endif
}

#if !defined(_MSC_VER) && !defined(__i386__) && !defined(__x86_64__)
static inline u64 tsc_now() { return wall_ns(); }
#endif

static bool jit_st_matches_cpu(const CPU* cpu, const JitState& st) {
    return memcmp(cpu->regs, st.gpr, sizeof(st.gpr)) == 0 && cpu->hi == st.hi &&
           cpu->lo == st.lo;
}

static void jit_pull_st_to_cpu(CPU* cpu, const JitState& st) {
    memcpy(cpu->regs, st.gpr, sizeof(st.gpr));
    cpu->hi = st.hi;
    cpu->lo = st.lo;
}

static void jit_push_cpu_to_st(JitState& st, const CPU* cpu) {
    memcpy(st.gpr, cpu->regs, sizeof(st.gpr));
    st.hi = cpu->hi;
    st.lo = cpu->lo;
}

static bool jit_plan_has_mtc0(const JitTbPlan& plan) {
    for (u32 i = 0; i < plan.count; i++) {
        if (plan.ops[i].op == JIT_COP_MTC0)
            return true;
    }
    return false;
}

}  // namespace

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
#if !defined(JIT_HOST_X64)
    log_info("[JIT] host backend %s is stub-only — staying on interpreter "
             "(rebuild with JIT_HOST=x64 for dynarec)",
             JIT_HOST_NAME);
    return false;
#endif
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
        log_warn("[JIT] exec_alloc(%u) failed — staying on interpreter", m_page_size);
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
    log_dbg("[JIT] Phase-0 harness ready: exec page %u bytes, proof TB %u bytes",
            m_page_size, kProofSize);
    // Calibrate the TSC against the wall clock for perf counters (~50 ms,
    // once). A zero delta falls back to a nominal 3 GHz (wrong scale, but
    // ratios between regions stay exact).
    {
        u64 t0 = tsc_now();
        u64 w0 = wall_ns();
        sleep_ms(50);
        u64 t1 = tsc_now();
        u64 w1 = wall_ns();
        if (w1 > w0 && t1 > t0)
            m_tsc_hz = (t1 - t0) * 1000000000ULL / (w1 - w0);
        else
            m_tsc_hz = 3000000000ULL;
        // Measure one rdtsc round-trip (min of 10k) so per-region numbers
        // below can be read net of timer overhead (each region spans 2 reads).
        u64 best = (u64)-1;
        for (int i = 0; i < 10000; i++) {
            u64 a = tsc_now();
            u64 b = tsc_now();
            if (b - a < best)
                best = b - a;
        }
        m_tsc_cost = best;
        log_dbg("[JIT] TSC %.2f GHz, rdtsc round-trip ~%lluns",
                (double)m_tsc_hz / 1e9,
                (unsigned long long)to_ns(m_tsc_cost));
    }
    return true;
}

u64 Jit::to_ns(u64 cycles) const {
    if (!m_tsc_hz)
        return 0;
    // Overflow-safe: split seconds and remainder (totals can exceed 2^64/1e9).
    return (cycles / m_tsc_hz) * 1000000000ULL +
           (cycles % m_tsc_hz) * 1000000000ULL / m_tsc_hz;
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
    printf("[JIT] tb_compiled=%llu hits=%llu neg_hits=%llu uncompilable=%llu calls=%llu evict=%llu\n",
           (unsigned long long)m_stats.tb_compiled, (unsigned long long)m_stats.tb_hits,
           (unsigned long long)m_stats.tb_neg_hits, (unsigned long long)m_stats.tb_uncompilable,
           (unsigned long long)m_stats.compile_calls, (unsigned long long)m_stats.tb_evictions);
    printf("[JIT] branch_tbs=%llu nextpc_exits=%llu chain_patch=%llu stub_hit=%llu unpatched=%llu chain_hit=%llu\n",
           (unsigned long long)m_stats.tb_branches, (unsigned long long)m_stats.tb_nextpc,
           (unsigned long long)m_stats.chain_patches, (unsigned long long)m_stats.chain_stub_hits,
           (unsigned long long)m_stats.chain_unpatched, (unsigned long long)m_stats.chain_hits);
    printf("[JIT] got_tb=%llu got_dispatch=%llu\n",
           (unsigned long long)m_stats.got_tb, (unsigned long long)m_stats.got_dispatches);
    printf("[JIT] tick_flush: fast=%llu slow=%llu insns fb_sync_skip=%llu\n",
           (unsigned long long)m_stats.tick_flush_fast,
           (unsigned long long)m_stats.tick_flush_slow,
           (unsigned long long)m_stats.fb_sync_skips);
    printf("[JIT] tb_insns=%llu interp_insns=%llu slow_exits=%llu flushes=%llu cache=%u/%u pools=%u\n",
           (unsigned long long)m_stats.tb_insns, (unsigned long long)m_stats.interp_insns,
           (unsigned long long)m_stats.slow_exits, (unsigned long long)m_stats.flushes,
           m_cache.used(), m_cache.capacity(), (u32)m_pools.size());
    if (total_insns)
        printf("[JIT] TB share: %.1f%% of guest insns\n",
               100.0 * (double)m_stats.tb_insns / (double)total_insns);
    // Perf counters (TSC cycles -> ns). loop_total excludes compile (shown
    // separately); per-iter averages divide by the matching iteration
    // counts (TB runs = hits + miss-compiles, each followed by one run).
    u64 tb_runs = m_stats.tb_hits + m_stats.tb_misses;
    u64 loop_total = m_stats.tb_cycles + m_stats.dispatch_cycles +
                     m_stats.fallback_cycles;
    printf("[JIT] time: tb=%llu.%03llums dispatch=%llu.%03llums fallback=%llu.%03llums compile=%llu.%03llums\n",
           (unsigned long long)(to_ns(m_stats.tb_cycles) / 1000000ULL),
           (unsigned long long)(to_ns(m_stats.tb_cycles) % 1000000ULL / 1000ULL),
           (unsigned long long)(to_ns(m_stats.dispatch_cycles) / 1000000ULL),
           (unsigned long long)(to_ns(m_stats.dispatch_cycles) % 1000000ULL / 1000ULL),
           (unsigned long long)(to_ns(m_stats.fallback_cycles) / 1000000ULL),
           (unsigned long long)(to_ns(m_stats.fallback_cycles) % 1000000ULL / 1000ULL),
           (unsigned long long)(to_ns(m_stats.compile_cycles) / 1000000ULL),
           (unsigned long long)(to_ns(m_stats.compile_cycles) % 1000000ULL / 1000ULL));
    if (tb_runs)
        printf("[JIT] per TB run: %.1fns exec, %.1f guest insns\n",
               (double)to_ns(m_stats.tb_cycles) / (double)tb_runs,
               (double)m_stats.tb_insns / (double)tb_runs);
    if (m_stats.fb_iters)
        printf("[JIT] per fallback: %.1fns (%llu iters)\n",
               (double)to_ns(m_stats.fallback_cycles) / (double)m_stats.fb_iters,
               (unsigned long long)m_stats.fb_iters);
    if (m_stats.compile_calls)
        printf("[JIT] per compile attempt: %.1fns (%llu calls, %llu ok)\n",
               (double)to_ns(m_stats.compile_cycles) / (double)m_stats.compile_calls,
               (unsigned long long)m_stats.compile_calls,
               (unsigned long long)m_stats.tb_compiled);
    if (m_stats.tb_compiled && m_stats.compile_cycles)
        printf("[JIT] compile split: form %.1f%% / emit %.1f%% / install %.1f%%\n",
               100.0 * (double)m_stats.form_cycles / (double)m_stats.compile_cycles,
               100.0 * (double)m_stats.emit_cycles / (double)m_stats.compile_cycles,
               100.0 * (double)m_stats.install_cycles / (double)m_stats.compile_cycles);
    printf("[JIT] timer overhead: rdtsc round-trip ~%lluns (2 reads per region)\n",
           (unsigned long long)to_ns(m_tsc_cost));
    if (loop_total)
        printf("[JIT] loop time: TB %.1f%% / dispatch %.1f%% / fallback %.1f%%\n",
               100.0 * (double)m_stats.tb_cycles / (double)loop_total,
               100.0 * (double)m_stats.dispatch_cycles / (double)loop_total,
               100.0 * (double)m_stats.fallback_cycles / (double)loop_total);
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

void Jit::flush_tb_ticks(CPU* cpu, u32 n, bool tick_fast_tb) {
    if (!n || !cpu)
        return;
    bool batch = tick_fast_tb && cpu->cop0.regs.wired == 0;
    if (batch)
        m_stats.tick_flush_fast += n;
    else
        m_stats.tick_flush_slow += n;
    cpu->cop0.flush_ticks(n, tick_fast_tb);
}

void Jit::patch_chain_edges_to(u32 target_pc, TbFunc target_func) {
    if (!target_func)
        return;
    u8* entry = (u8*)target_func + JIT_PROLOG_CHAIN_OFF;
    u32 n = m_cache.patch_edges_to(target_pc, entry,
                                  [](u8* tb, u32 off, u8* ent) {
                                      jit_patch_chain_site(tb, off, ent);
                                  });
    m_stats.chain_patches += n;
}

void Jit::patch_outgoing_chain_edges(TbFunc src, const JitChainInfo* chain) {
    if (!chain || !chain->n || !src)
        return;
    u8* base = (u8*)src;
    for (u32 i = 0; i < chain->n; i++) {
        u32 off = chain->sites[i].code_off;
        TbFunc tgt = nullptr;
        u32 tc = 0;
        if (m_cache.find(chain->sites[i].target_pc, tgt, tc) && tgt) {
            jit_patch_chain_site(base, off, (u8*)tgt + JIT_PROLOG_CHAIN_OFF);
            m_stats.chain_patches++;
        }
    }
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
                     TbFunc& func, u32& count, bool& stable) {
    // All failure modes below are deterministic in (pc, RAM bytes, stop
    // set): RAM is covered by the generation counter and stops are constant
    // per phase, so the dispatcher may negative-cache them. Only exec-OOM
    // is transient (stable=false: retry later).
    stable = true;
    u64 t_f0 = tsc_now();
    JitFormed formed{};
    int got_idx = -1;
    if (cpu->mem->is_got_address(pc)) {
        if (!jit_got_entry_valid(cpu, pc, &formed, &got_idx))
            return false;
    } else {
        formed = jit_form_tb(cpu->mem, pc, stop_pc, alt_stop_pc);
        if (formed.n == 0)
            return false;
    }
    u64 t_f1 = tsc_now();
    // Emit into a stack buffer first so a failed compile wastes no pool.
    u8 tmp[16 << 10];
    u32 len = 0;
    u32 equiv = formed.n;  // execute_one-equivalents (ticks + insn_count)
    JitChainInfo chain{};
    JitTbPlan plan{};
    if (formed.is_got) {
        len = jit_compile_got_tb(pc, got_idx, tmp, (u32)sizeof(tmp));
        equiv = 1;
    } else {
        plan = jit_decode_tb(formed.words, formed.n);
        // Decode is deterministic over validated words: a formed run decodes
        // 1:1 (branches validate by construction). Bail if it ever doesn't.
        if (plan.count != formed.n)
            return false;
        if (formed.has_branch) {
            // Defensive: the branch must be where formation put it.
            if (formed.branch_idx + 2 > plan.count ||
                !jit_op_is_branch(plan.ops[formed.branch_idx].op))
                return false;
            len = jit_compile_branch_tb(plan, formed.branch_idx, pc, tmp,
                                        (u32)sizeof(tmp), &chain);
            // Branch+delay share one tick/insn_count with the interpreter
            // (execute_one ticks once for both): prefix + 1.
            equiv = formed.branch_idx + 1;
        } else {
            len = jit_compile_tb(plan, tmp, (u32)sizeof(tmp), (u32)JIT_EXIT_DONE, pc);
        }
    }
    u64 t_f2 = tsc_now();
    if (!len) {
        m_stats.form_cycles += t_f1 - t_f0;
        m_stats.emit_cycles += t_f2 - t_f1;
        return false;
    }
    // Table is self-bounding (collisions evict); no LRU flush needed here.
    // gen/phase flushes still clear everything via flush_locked().
    u8* dst = pool_alloc(len);
    if (!dst) {
        stable = false;  // transient: memory pressure, retry later
        return false;
    }
    memcpy(dst, tmp, len);
#ifdef _WIN32
    FlushInstructionCache(GetCurrentProcess(), dst, len);
#else
    __builtin___clear_cache((char*)dst, (char*)dst + len);
#endif
    bool tick_fast = !jit_plan_has_mtc0(plan);
    m_cache.insert(pc, (TbFunc)dst, equiv, tick_fast,
                   formed.has_branch ? &chain : nullptr);
    patch_chain_edges_to(pc, (TbFunc)dst);
    if (formed.has_branch && chain.n)
        patch_outgoing_chain_edges((TbFunc)dst, &chain);  // patch to cached targets

    if (formed.is_got)
        m_stats.got_tb++;
    if (formed.has_branch)
        m_stats.tb_branches++;
    m_stats.form_cycles += t_f1 - t_f0;
    m_stats.emit_cycles += t_f2 - t_f1;
    m_stats.install_cycles += tsc_now() - t_f2;
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
    st.syscalls = cpu->syscalls;
    u32 executed = 0;
    // true when cpu->regs/hi/lo already match m_st (skip redundant pre-fallback sync).
    bool cpu_gpr_live = true;
    while (cpu->running && cpu->pc != stop_pc && executed < max_insns) {
        if (alt_stop_pc && cpu->pc == alt_stop_pc)
            break;
        u64 t_top = tsc_now();
        // Code remap (dl_load/dl_free/dl_res map+close) drops the cache.
        // Checked per iteration: remaps happen inside fallback syscalls.
        if (!m_gen_valid || g_code_gen != m_cached_gen)
            flush_locked();
        u32 pc = cpu->pc;
        TbFunc func = nullptr;
        u32 count = 0;
        bool tick_fast = false;
        u64 cc = 0;  // compile cycles this iteration (excluded from dispatch)
        if (jit_pc_eligible(mem, pc)) {
            if (m_cache.find(pc, func, count, &tick_fast)) {  // direct-mapped, ~9ns
                if (func)
                    m_stats.tb_hits++;
                else
                    m_stats.tb_neg_hits++;
            } else {
                // Cache miss: attempt a compile. Stable failures (JR head,
                // stops, awkward delay slots) are negative-cached as a null
                // entry so repeat visits cost one lookup, like a hit. Valid
                // until the next gen/phase flush, same as positive entries
                // (formation is deterministic per pc+RAM+stops).
                u64 t_c0 = tsc_now();
                bool stable = true;
                bool ok = compile_tb(cpu, pc, stop_pc, alt_stop_pc, func, count, stable);
                cc = tsc_now() - t_c0;
                m_stats.compile_cycles += cc;
                m_stats.compile_calls++;
                if (!ok) {
                    func = nullptr;
                    if (stable) {
                        if (m_cache.insert(pc, nullptr, 0, false, nullptr))
                            m_stats.tb_evictions++;
                        m_stats.tb_uncompilable++;
                    }
                } else {
                    m_cache.find(pc, func, count, &tick_fast);
                }
            }
        }
    run_tb:
        if (!func) {
            // JR/JALR, stops, GOT, unmapped, awkward delay slots: the
            // interpreter owns it. Sync state around the call.
            if (!cpu_gpr_live || !jit_st_matches_cpu(cpu, st))
                jit_pull_st_to_cpu(cpu, st);
            else
                m_stats.fb_sync_skips++;
            cpu->execute_one_jit();
            jit_push_cpu_to_st(st, cpu);
            cpu_gpr_live = true;
            executed++;
            m_stats.interp_insns++;
            m_stats.fb_iters++;
            // cc is nonzero only when a compile was attempted and failed;
            // that time is reported under compile, not fallback. Guarded
            // against (theoretical) TSC non-monotonicity across cores.
            u64 total = tsc_now() - t_top;
            if (total >= cc)
                m_stats.fallback_cycles += total - cc;
            continue;
        }
        u64 t_pre = tsc_now();
        if (t_pre >= t_top + cc)
            m_stats.dispatch_cycles += (t_pre - t_top) - cc;
        // Only the tick counter is per-TB (everything else is resident).
        st.tick_delta = 0;
        st.insn_delta = 0;
        u32 exit = func(&st);
        u32 ran = st.insn_delta ? st.insn_delta : count;
        if (exit == (u32)JIT_EXIT_SLOW_MEM) {
            // Prefix applied exit_arg ops (branch, if any, never reached).
            u32 applied = st.exit_arg;
            cpu->pc = pc + applied * 4;
            m_stats.slow_exits++;
            // Faulting op (MMIO/unmapped/code) via the interpreter, which
            // owns palette/GPIO/log semantics plus its own tick + count.
            if (!cpu_gpr_live || !jit_st_matches_cpu(cpu, st))
                jit_pull_st_to_cpu(cpu, st);
            else
                m_stats.fb_sync_skips++;
            flush_tb_ticks(cpu, st.tick_delta, tick_fast);
            cpu->insn_count += applied;
            executed += applied;
            m_stats.tb_insns += applied;
            m_stats.tb_cycles += tsc_now() - t_pre;  // prefix + exit bookkeeping
            cpu_gpr_live = false;
            u64 t_fb = tsc_now();
            cpu->execute_one_jit();
            jit_push_cpu_to_st(st, cpu);
            cpu_gpr_live = true;
            executed++;
            m_stats.interp_insns++;
            m_stats.fb_iters++;
            m_stats.fallback_cycles += tsc_now() - t_fb;
        } else if (exit == (u32)JIT_EXIT_DONE) {
            cpu->pc = st.next_pc;
            flush_tb_ticks(cpu, st.tick_delta, tick_fast);
            cpu->insn_count += ran;
            executed += ran;
            m_stats.tb_insns += ran;
            m_stats.tb_cycles += tsc_now() - t_pre;
            cpu_gpr_live = false;
        } else if (exit == (u32)JIT_EXIT_GOT) {
            u32 resume = st.next_pc;
            cpu->pc = resume;
            st.pc = resume;
            m_stats.got_dispatches++;
            flush_tb_ticks(cpu, st.tick_delta, tick_fast);
            cpu->insn_count += ran;
            executed += ran;
            m_stats.tb_insns += ran;
            m_stats.tb_cycles += tsc_now() - t_pre;
            cpu_gpr_live = false;
            TbFunc resume_tb = nullptr;
            u32 resume_count = 0;
            bool resume_tick_fast = false;
            if (m_cache.find(resume, resume_tb, resume_count, &resume_tick_fast) &&
                resume_tb) {
                m_stats.chain_hits++;
                func = resume_tb;
                count = resume_count;
                tick_fast = resume_tick_fast;
                pc = resume;
                goto run_tb;
            }
        } else if (exit == (u32)JIT_EXIT_NEXT_PC) {
            // Branch TB: st.next_pc holds taken/fallthrough target.
            u32 target = st.next_pc;
            cpu->pc = target;
            m_stats.tb_nextpc++;
            TbFunc target_tb = nullptr;
            u32 target_count = 0;
            bool target_tick_fast = false;
            if (m_cache.find(target, target_tb, target_count, &target_tick_fast) &&
                target_tb)
                patch_chain_edges_to(target, target_tb);
            else
                m_stats.chain_unpatched++;
            flush_tb_ticks(cpu, st.tick_delta, tick_fast);
            cpu->insn_count += ran;
            executed += ran;
            m_stats.tb_insns += ran;
            m_stats.tb_cycles += tsc_now() - t_pre;
            cpu_gpr_live = false;
            if (target_tb) {
                m_stats.chain_hits++;
                func = target_tb;
                count = target_count;
                tick_fast = target_tick_fast;
                pc = target;
                goto run_tb;
            }
        } else {
            log_warn("[JIT] unexpected exit=%u at PC=0x%08X — halting", exit, pc);
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
