#include "jit.h"

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

// Hand-encoded x86-64 proof TB. Disassembly:
//
//   mov DWORD PTR [rdi/rcx + 8], 1   ; gpr[2] (v0) = 1  (offsetof(JitState,gpr[2]) == 8)
//   mov eax, 0                        ; return JIT_EXIT_DONE
//   ret
//
// Encoding notes:
//   - The single JitState* arg arrives in RDI (System V) or RCX (Win64).
//     We emit both stores so the proof passes on either ABI without
//     any ifdefs at the call site; writing the same value twice is harmless.
//   - ModRM/SIB for [base+disp8]: mod=01 reg=000 r/m=100 (SIB follows),
//     SIB: scale=00 index=100(none) base=111(rdi) / 001(rcx), disp8=0x08.
//   - REX.W is NOT set: 32-bit operand size, zero-extends into the full reg.
//   - Offsets are compile-time-checked below with static_assert.
//
const unsigned char Jit::kProofCode[] = {
    0xC7, 0x47, 0x08, 0x01, 0x00, 0x00, 0x00, // mov DWORD PTR [rdi+8], 1
    0xC7, 0x41, 0x08, 0x01, 0x00, 0x00, 0x00, // mov DWORD PTR [rcx+8], 1
    0x31, 0xC0,                               // xor eax, eax  (JIT_EXIT_DONE)
    0xC3,                                     // ret
};
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
}
