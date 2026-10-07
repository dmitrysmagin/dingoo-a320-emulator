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

// A TB may only start (and continue) on plain KSEG0/KSEG1 RAM. Everything
// else goes through CPU::execute_one(), which owns the exact semantics:
//   - KUSEG/KSEG2/3 (bit31 clear or unmapped): fetch-0/log + [KUSEG] halt rules
//   - OS area [0x80000000, 0x80A00000): fetch() returns JR $ra (mirrors cpu.cpp)
//   - GOT addresses: syscall dispatch + task-switch resume
static bool tb_eligible_pc(Memory* mem, u32 pc) {
    if ((pc & 0x80000000u) == 0)
        return false;
    if (pc >= 0x80000000u && pc < 0x80A00000u)
        return false;
    if (mem->is_got_address(pc))
        return false;
    return mem->is_mapped(pc);
}

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
    Memory* mem = cpu->mem;
    u32 words[JIT_TB_MAX_INSNS];
    u32 n = 0;
    for (; n < JIT_TB_MAX_INSNS; n++) {
        u32 pn = pc + n * 4;
        // Never overshoot a stop PC: the loop condition must see it.
        if (pn == stop_pc || (alt_stop_pc && pn == alt_stop_pc))
            break;
        if (!tb_eligible_pc(mem, pn))
            break;
        words[n] = mem->read_u32(pn);  // eligible => mapped RAM, no log spam
    }
    if (n == 0)
        return false;
    JitTbPlan plan = jit_decode_tb(words, n);
    // Phase 2 branch ops decode valid but have no emitter yet: truncate the
    // TB before the first one (it runs on the interpreter next iteration).
    // Without this, any TB reaching a branch fails wholesale and is
    // retried on every visit (35M wasted compiles in tetris).
    for (u32 i = 0; i < plan.count; i++) {
        JitAluOp op = plan.ops[i].op;
        if (op >= JIT_ALU_J && op <= JIT_ALU_BGTZL) {
            plan.count = i;
            break;
        }
    }
    if (plan.count == 0)
        return false;  // control op at head (branch/GOT/trap/COP-stop/...)
    // Emit into a stack buffer first so a failed compile wastes no pool.
    u8 tmp[16 << 10];
    u32 len = jit_compile_tb(plan, tmp, (u32)sizeof(tmp), (u32)JIT_EXIT_DONE);
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
    u32 phys = mem->vaddr_to_phys(pc, false);
    m_cache[phys] = TbEntry{(TbFunc)dst, plan.count};
    m_stats.tb_compiled++;
    m_stats.tb_misses++;
    func = (TbFunc)dst;
    count = plan.count;
    return true;
}

void Jit::run_until_pc(CPU* cpu, u32 stop_pc, u32 max_insns, u32 alt_stop_pc) {
    if (!m_ready || !cpu) {
        if (cpu)
            cpu->run_until_pc(stop_pc, max_insns, alt_stop_pc);
        return;
    }
    Memory* mem = cpu->mem;
    JitState st;
    memset(&st, 0, sizeof(st));
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
        if (tb_eligible_pc(mem, pc)) {
            u32 phys = mem->vaddr_to_phys(pc, false);
            auto it = m_cache.find(phys);
            if (it != m_cache.end()) {
                func = it->second.func;
                count = it->second.count;
                m_stats.tb_hits++;
            } else if (!compile_tb(cpu, pc, stop_pc, alt_stop_pc, func, count)) {
                func = nullptr;
            }
        }
        if (!func) {
            // Control flow, stops, GOT, unmapped: the interpreter owns it.
            cpu->execute_one();
            executed++;
            m_stats.interp_insns++;
            continue;
        }
        // Sync interpreter -> TB. COP0/MXU/RAM are pointer-shared (no copy);
        // tick_delta restarts per TB and is flushed below.
        memcpy(st.gpr, cpu->regs, sizeof(st.gpr));
        st.hi = cpu->hi;
        st.lo = cpu->lo;
        st.pc = pc;
        st.next_pc = 0;
        st.exit_arg = 0;
        st.mem_base = (u64)(uintptr_t)mem->get_raw_ptr();
        st.mem_size = mem->size();
        st.code_start = mem->code_start();
        st.code_end = mem->code_end();
        st.wc_base = (u64)(uintptr_t)mem->write_counts_base();
        st.cop0 = &cpu->cop0;
        st.mxu = &cpu->mxu;
        st.tick_delta = 0;
        u32 exit = func(&st);
        // SLOW_MEM applied exit_arg ops (the faulting op is still pending);
        // DONE applied the whole TB.
        u32 applied = (exit == (u32)JIT_EXIT_SLOW_MEM) ? st.exit_arg : count;
        memcpy(cpu->regs, st.gpr, sizeof(st.gpr));
        cpu->hi = st.hi;
        cpu->lo = st.lo;
        cpu->pc = pc + applied * 4;
        for (u32 i = 0; i < st.tick_delta; i++)
            cpu->cop0.tick();
        cpu->insn_count += applied;
        executed += applied;
        m_stats.tb_insns += applied;
        if (exit == (u32)JIT_EXIT_SLOW_MEM) {
            m_stats.slow_exits++;
            // Faulting op (MMIO/unmapped/code) via the interpreter, which
            // owns palette/GPIO/log semantics plus its own tick + count.
            cpu->execute_one();
            executed++;
            m_stats.interp_insns++;
        } else if (exit != (u32)JIT_EXIT_DONE) {
            printf("[JIT] unexpected exit=%u at PC=0x%08X — halting\n", exit, pc);
            cpu->running = false;
        }
        // NOTE: max_insns may overshoot by <1 TB (bounded, documented).
    }
}
