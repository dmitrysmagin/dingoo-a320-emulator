#include "jit_test.h"
#include "emit.h"
#include "frontend.h"

#include <cstdio>
#include <cstring>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace {

struct Rng {
    u32 s;
    u32 next() {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return s;
    }
    u32 word() {
        switch (next() % 8) {
        case 0: return 0u;
        case 1: return 1u;
        case 2: return 0xFFFFFFFFu;
        case 3: return 0x80000000u;
        case 4: return 0x7FFFFFFFu;
        case 5: return (u32)(next() & 0xFFFFu);
        case 6: return (u32)(next() & 0xFFu);
        default: return next();
        }
    }
    u32 reg() { return next() % 32; }
    u32 nzreg() { return 1 + next() % 31; }
    u32 sa() { return next() % 32; }
};

typedef u32 (*TbFunc)(JitState*);

struct ExecPage {
    void* p;
    u32 size;
    ExecPage() : p(0), size(0) {}
    bool alloc(u32 want) {
#ifdef _WIN32
        SYSTEM_INFO si; GetSystemInfo(&si);
        size = (u32)si.dwPageSize;
#else
        long ps = sysconf(_SC_PAGESIZE);
        size = (ps > 0) ? (u32)ps : 4096;
#endif
        if (size < want) size = want;
#ifdef _WIN32
        p = VirtualAlloc(0, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        return p != 0;
#else
        p = mmap(0, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        return p != MAP_FAILED && p != 0;
#endif
    }
    void done() {
        if (!p) return;
#ifdef _WIN32
        VirtualFree(p, 0, MEM_RELEASE);
#else
        munmap(p, size);
#endif
        p = 0;
    }
};

// Layout-compatible with the emitter: gpr[32] then hi/lo at JIT_OFF_HI/LO.
// (JitState's exit fields sit at the same offsets; tests use EAX return.)
struct AluState {
    u32 gpr[32];
    u32 exit_code;
    u32 exit_arg;
    u32 next_pc;
    u32 pc;
    u32 hi;
    u32 lo;
};

u32 run_tb(ExecPage& pg, const JitTbPlan& plan, AluState& st, u32* out_len) {
    u8* buf = (u8*)pg.p;
    u32 len = jit_compile_tb(plan, buf, pg.size, (u32)JIT_EXIT_DONE);
    if (!len)
        return 0xDEADDEADu;
    if (out_len) *out_len = len;
#ifdef _WIN32
    FlushInstructionCache(GetCurrentProcess(), buf, len);
#else
    __builtin___clear_cache((char*)buf, (char*)buf + len);
#endif
    TbFunc fn = (TbFunc)buf;
    return fn((JitState*)&st);
}

int failures = 0;
int passes = 0;
bool g_verbose = false;
#define CHECK(cond, ...) do { \
    if (cond) { passes++; } \
    else { failures++; printf("[FAIL] " __VA_ARGS__); printf("\n"); } \
} while (0)

void diff_one(ExecPage& pg, const JitTbPlan& plan, AluState init, const char* tag) {
    AluState a = init, b = init;
    u32 hi_b = init.hi, lo_b = init.lo;
    jit_run_reference(plan, b.gpr, &hi_b, &lo_b);
    b.hi = hi_b; b.lo = lo_b;
    u32 len = 0;
    u32 exit = run_tb(pg, plan, a, &len);
    CHECK(exit == (u32)JIT_EXIT_DONE, "%s: exit=0x%08X", tag, exit);
    if (exit != (u32)JIT_EXIT_DONE)
        return;
    for (int r = 0; r < 32; r++)
        CHECK(a.gpr[r] == b.gpr[r], "%s: r%d emit=0x%08X ref=0x%08X",
              tag, r, a.gpr[r], b.gpr[r]);
    CHECK(a.hi == b.hi, "%s: hi emit=0x%08X ref=0x%08X", tag, a.hi, b.hi);
    CHECK(a.lo == b.lo, "%s: lo emit=0x%08X ref=0x%08X", tag, a.lo, b.lo);
    (void)len;
}

u32 w_special(u32 rs, u32 rt, u32 rd, u32 sa, u32 func) {
    return (rs << 21) | (rt << 16) | (rd << 11) | ((sa & 0x1F) << 6) | (func & 0x3F);
}
u32 w_imm(u32 op, u32 rs, u32 rt, u32 imm16) {
    return (op << 26) | (rs << 21) | (rt << 16) | (imm16 & 0xFFFFu);
}
u32 w_ext(u32 rs, u32 rt, u32 msb, u32 lsb) {
    return (0x1Fu << 26) | (rs << 21) | (rt << 16) |
           ((msb & 0x1F) << 11) | ((lsb & 0x1F) << 6) | 0x00u;
}
u32 w_ins(u32 rs, u32 rt, u32 msb, u32 lsb) {
    return (0x1Fu << 26) | (rs << 21) | (rt << 16) |
           ((msb & 0x1F) << 11) | ((lsb & 0x1F) << 6) | 0x04u;
}
u32 w_special2_mul(u32 rs, u32 rt, u32 rd) {
    return (0x1Cu << 26) | (rs << 21) | (rt << 16) | (rd << 11) | 0x02u;
}

void test_single_ops(ExecPage& pg) {
    printf("[jit-test] single: specials...\n"); fflush(stdout);
    {   // Byte-dump probe: NOP-only TB shows prolog+epilog in isolation.
        u32 w = w_special(0, 0, 0, 0, 0x00);
        JitTbPlan plan = jit_decode_tb(&w, 1);
        u8* buf = (u8*)pg.p;
        u32 len = jit_compile_tb(plan, buf, pg.size, (u32)JIT_EXIT_DONE);
        printf("[jit-test] nop-tb len=%u bytes:", len); fflush(stdout);
        for (u32 i = 0; i < len && i < 64; i++) { printf(" %02X", buf[i]); fflush(stdout); }
        printf("\n"); fflush(stdout);
        {   // SLL $8,$9,3 probe.
            u32 w2 = w_special(9, 9, 8, 3, 0x00);
            JitTbPlan p2 = jit_decode_tb(&w2, 1);
            u32 l2 = jit_compile_tb(p2, buf, pg.size, (u32)JIT_EXIT_DONE);
            printf("[jit-test] sll-tb len=%u bytes:", l2); fflush(stdout);
            for (u32 i = 0; i < l2 && i < 64; i++) { printf(" %02X", buf[i]); fflush(stdout); }
            printf("\n"); fflush(stdout);
            AluState s; memset(&s, 0, sizeof(s));
            s.gpr[9] = 0x12345678u;
            TbFunc fn = (TbFunc)buf;
#ifdef _WIN32
            FlushInstructionCache(GetCurrentProcess(), buf, l2);
#else
            __builtin___clear_cache((char*)buf, (char*)buf + l2);
#endif
            printf("[jit-test] calling sll-tb...\n"); fflush(stdout);
            u32 ex = fn((JitState*)&s);
            printf("[jit-test] sll-tb exit=%u r8=0x%08X\n", ex, s.gpr[8]); fflush(stdout);
        }
    }
    Rng rng; rng.s = 0x12345678u;
    const u32 funcs[] = {0x00,0x02,0x03,0x0A,0x0B,0x0F,0x10,0x11,0x12,0x13,
        0x18,0x19,0x1A,0x1B,0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x2A,0x2B};
    for (u32 f : funcs) {
        { printf("[jit-test] func 0x%02X\n", f); fflush(stdout); }
        for (int iter = 0; iter < 12; iter++) {
            u32 rs = rng.reg(), rt = rng.reg(), rd = rng.nzreg();
            u32 w = w_special(rs, rt, rd, rng.sa(), f);
            JitTbPlan plan = jit_decode_tb(&w, 1);
            char tag[96];
            snprintf(tag, sizeof(tag), "special f=0x%02X", f);
            AluState init;
            for (int r = 0; r < 32; r++) init.gpr[r] = rng.word();
            init.gpr[0] = 0;
            init.hi = rng.word(); init.lo = rng.word();
            diff_one(pg, plan, init, tag);
        }
    }
    if (g_verbose) printf("[jit-test] SPECIAL done\n");
    const u32 imm_ops[] = {0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F};
    for (u32 op : imm_ops) {
        for (int iter = 0; iter < 12; iter++) {
            u32 w = w_imm(op, rng.reg(), rng.nzreg(), rng.next() & 0xFFFFu);
            JitTbPlan plan = jit_decode_tb(&w, 1);
            char tag[96];
            snprintf(tag, sizeof(tag), "imm op=0x%02X", op);
            AluState init;
            for (int r = 0; r < 32; r++) init.gpr[r] = rng.word();
            init.gpr[0] = 0;
            init.hi = rng.word(); init.lo = rng.word();
            diff_one(pg, plan, init, tag);
        }
    }
    if (g_verbose) printf("[jit-test] immediates done\n");
    for (int iter = 0; iter < 20; iter++) {
        u32 w = w_special2_mul(rng.reg(), rng.reg(), rng.nzreg());
        JitTbPlan plan = jit_decode_tb(&w, 1);
        AluState init;
        for (int r = 0; r < 32; r++) init.gpr[r] = rng.word();
        init.gpr[0] = 0;
        init.hi = rng.word(); init.lo = rng.word();
        diff_one(pg, plan, init, "MUL");
    }
    if (g_verbose) printf("[jit-test] MUL done\n");
    for (int iter = 0; iter < 40; iter++) {
        u32 lsb = rng.next() % 32, msb = rng.next() % 32;
        if (iter < 30 && msb < lsb) { u32 t = msb; msb = lsb; lsb = t; }
        u32 w = (iter & 1) ? w_ins(rng.reg(), rng.nzreg(), msb, lsb)
                           : w_ext(rng.reg(), rng.nzreg(), msb, lsb);
        JitTbPlan plan = jit_decode_tb(&w, 1);
        char tag[96];
        snprintf(tag, sizeof(tag), "%s msb=%u lsb=%u", (iter & 1) ? "INS" : "EXT", msb, lsb);
        AluState init;
        for (int r = 0; r < 32; r++) init.gpr[r] = rng.word();
        init.gpr[0] = 0;
        init.hi = rng.word(); init.lo = rng.word();
        diff_one(pg, plan, init, tag);
    }
    if (g_verbose) printf("[jit-test] EXT/INS done\n");
}

void test_multi_tb(ExecPage& pg) {
    // Mixed 2..8-op TBs with data dependencies + HI/LO flow.
    Rng rng; rng.s = 0x87654321u;
    const u32 pool[] = {
        0x00221821u,  // addu v1,v1,v0-ish shape (fixed regs, varied below)
        0x00000000u,  // nop (sll $0,$0,0)
    };
    (void)pool;
    for (int tb = 0; tb < 60; tb++) {
        u32 n = 2 + rng.next() % 7;
        u32 words[8];
        for (u32 i = 0; i < n; i++) {
            u32 pick = rng.next() % 10;
            u32 rs = rng.nzreg(), rt = rng.nzreg(), rd = rng.nzreg();
            switch (pick) {
            case 0: words[i] = w_special(rs, rt, rd, rng.sa(), 0x21); break;  // addu
            case 1: words[i] = w_special(rs, rt, rd, 0, 0x25); break;         // or
            case 2: words[i] = w_special(rs, rt, rd, 0, 0x26); break;         // xor
            case 3: words[i] = w_imm(0x09, rs, rt, rng.next() & 0xFFFFu); break;  // addiu
            case 4: words[i] = w_imm(0x0D, rs, rt, rng.next() & 0xFFFFu); break;  // ori
            case 5: words[i] = w_special(rs, rt, rd, rng.sa(), 0x00); break;  // sll
            case 6: words[i] = w_special(rs, rt, rd, rng.sa(), 0x03); break;  // sra
            case 7: words[i] = w_special(rs, rt, rd, 0, 0x2B); break;         // sltu
            case 8: words[i] = w_special(rs, 0, rd, 0, 0x13); break;          // mtlo rs
            default: words[i] = w_special(0, rs, rd, 0, 0x12); break;         // mflo rd
            }
        }
        JitTbPlan plan = jit_decode_tb(words, n);
        AluState init;
        for (int r = 0; r < 32; r++) init.gpr[r] = rng.word();
        init.gpr[0] = 0;
        init.hi = rng.word(); init.lo = rng.word();
        diff_one(pg, plan, init, "multi");
    }
    if (g_verbose) printf("[jit-test] multi-op TBs done\n");
}

// ---- Phase 3 memory discharge tests ----

// Full JitState mirror for executing mem TBs (ALU-only AluState is a prefix
// of this layout; mem TBs read mem_base and beyond, so they need it all).
struct MemState {
    u32 gpr[32];
    u32 exit_code;
    u32 exit_arg;
    u32 next_pc;
    u32 pc;
    u32 hi;
    u32 lo;
    u64 mem_base;
    u32 mem_size;
    u32 code_start;
    u32 code_end;
    u64 wc_base;
};
static_assert(offsetof(MemState, gpr) == offsetof(JitState, gpr), "memstate gpr");
static_assert(offsetof(MemState, exit_arg) == offsetof(JitState, exit_arg), "memstate exit_arg");
static_assert(offsetof(MemState, hi) == offsetof(JitState, hi), "memstate hi");
static_assert(offsetof(MemState, lo) == offsetof(JitState, lo), "memstate lo");
static_assert(offsetof(MemState, mem_base) == offsetof(JitState, mem_base), "memstate base");
static_assert(offsetof(MemState, mem_size) == offsetof(JitState, mem_size), "memstate size");
static_assert(offsetof(MemState, code_start) == offsetof(JitState, code_start), "memstate cs");
static_assert(offsetof(MemState, code_end) == offsetof(JitState, code_end), "memstate ce");
static_assert(offsetof(MemState, wc_base) == offsetof(JitState, wc_base), "memstate wc");
static_assert(sizeof(MemState) == sizeof(JitState), "memstate size");

static u32 run_mem_tb(ExecPage& pg, const JitTbPlan& plan, MemState& st) {
    u8* buf = (u8*)pg.p;
    u32 len = jit_compile_tb(plan, buf, pg.size, (u32)JIT_EXIT_DONE);
    if (!len)
        return 0xDEADDEADu;
#ifdef _WIN32
    FlushInstructionCache(GetCurrentProcess(), buf, len);
#else
    __builtin___clear_cache((char*)buf, (char*)buf + len);
#endif
    TbFunc fn = (TbFunc)buf;
    return fn((JitState*)&st);
}

static u32 w_mem(u32 op, u32 rs, u32 rt, u32 imm16) {
    return (op << 26) | (rs << 21) | (rt << 16) | (imm16 & 0xFFFFu);
}

// Diff one mem TB: emitted execution (MemState over ra/wca) vs the C++
// reference (JitMemState over rb/wcb). RAM/WC start as copies.
static void diff_mem(ExecPage& pg, const JitTbPlan& plan,
                     const std::vector<u8>& ram_init,
                     u32 code_start, u32 code_end,
                     MemState init_regs, const char* tag) {
    std::vector<u8> ra = ram_init, rb = ram_init;
    u32 pages = (u32)(ram_init.size() + 4095) / 4096;
    std::vector<u32> wca(pages, 0), wcb(pages, 0);

    MemState a = init_regs;
    a.mem_base = (u64)(uintptr_t)ra.data();
    a.mem_size = (u32)ra.size();
    a.code_start = code_start;
    a.code_end = code_end;
    a.wc_base = (u64)(uintptr_t)wca.data();
    a.exit_arg = 0xDEADBEEFu;

    JitMemState ref;
    memcpy(ref.regs, init_regs.gpr, sizeof(ref.regs));
    ref.hi = init_regs.hi; ref.lo = init_regs.lo;
    ref.ram = rb.data(); ref.ram_size = (u32)rb.size();
    ref.code_start = code_start; ref.code_end = code_end;
    ref.wc = wcb.data(); ref.wc_pages = pages;
    u32 ref_fail = 0xDEADBEEFu;
    u32 ref_exit = jit_run_mem_reference(plan, ref, ref_fail);

    u32 exit = run_mem_tb(pg, plan, a);
    CHECK(exit == ref_exit, "%s: exit emit=%u ref=%u", tag, exit, ref_exit);
    if (exit == 0xDEADDEADu)
        return;  // compile failed; nothing else to compare
    if (exit == (u32)JIT_EXIT_SLOW_MEM)
        CHECK(a.exit_arg == ref_fail, "%s: exit_arg emit=%u ref=%u",
              tag, a.exit_arg, ref_fail);
    for (int r = 0; r < 32; r++)
        CHECK(a.gpr[r] == ref.regs[r], "%s: r%d emit=0x%08X ref=0x%08X",
              tag, r, a.gpr[r], ref.regs[r]);
    CHECK(a.hi == ref.hi, "%s: hi emit=0x%08X ref=0x%08X", tag, a.hi, ref.hi);
    CHECK(a.lo == ref.lo, "%s: lo emit=0x%08X ref=0x%08X", tag, a.lo, ref.lo);
    CHECK(ra == rb, "%s: RAM mismatch", tag);
    CHECK(wca == wcb, "%s: write_counts mismatch", tag);
}

static MemState base_mem_regs(Rng& rng) {
    MemState s;
    memset(&s, 0, sizeof(s));
    for (int r = 0; r < 32; r++) s.gpr[r] = rng.word();
    s.gpr[0] = 0;
    s.hi = rng.word(); s.lo = rng.word();
    return s;
}

void test_mem_fast(ExecPage& pg) {
    // 64KB RAM, code section [0x1000, 0x2000). Fast addrs only.
    Rng rng; rng.s = 0x13572468u;
    std::vector<u8> ram(64 * 1024);
    for (size_t i = 0; i < ram.size(); i++) ram[i] = (u8)(rng.next() >> (rng.next() & 7));
    const u32 mem_ops[] = {0x20, 0x21, 0x23, 0x24, 0x25, 0x28, 0x29, 0x2B};
    const u32 bases[] = {0x80000000u, 0xA0000000u, 0x00000000u};  // KSEG0/KSEG1/KUSEG
    (void)bases;
    for (int tb = 0; tb < 120; tb++) {
        u32 n = 1 + rng.next() % 5;
        u32 words[6];
        // Random regs/words; retry until the reference reports all-fast
        // (bounded tries), then diff emitted-vs-reference.
        JitTbPlan plan;
        MemState init;
        bool all_fast = false;
        for (int attempt = 0; attempt < 20 && !all_fast; attempt++) {
            for (u32 i = 0; i < n; i++) {
                u32 mop = mem_ops[rng.next() % 8];
                bool is_store = (mop == 0x28 || mop == 0x29 || mop == 0x2B);
                u32 rs = rng.reg(), rt = is_store ? rng.reg() : rng.nzreg();
                words[i] = w_mem(mop, rs, rt, rng.next() & 0xFFFFu);
            }
            plan = jit_decode_tb(words, n);
            init = base_mem_regs(rng);
            // Bias rs values into KSEG0 so most attempts land in fast RAM.
            for (u32 i = 0; i < n; i++) {
                u32 rs = plan.ops[i].rs;
                if (rs != 0 && (rng.next() & 3))
                    init.gpr[rs] = 0x80000000u + (rng.next() % (u32)ram.size());
            }
            JitMemState probe;
            memcpy(probe.regs, init.gpr, sizeof(probe.regs));
            probe.hi = init.hi; probe.lo = init.lo;
            // Probe against a scratch copy (reference mutates RAM).
            std::vector<u8> scratch = ram;
            probe.ram = scratch.data(); probe.ram_size = (u32)scratch.size();
            probe.code_start = 0x1000; probe.code_end = 0x2000;
            std::vector<u32> wcs((ram.size() + 4095) / 4096, 0);
            probe.wc = wcs.data(); probe.wc_pages = (u32)wcs.size();
            u32 fail = 0;
            all_fast = (jit_run_mem_reference(plan, probe, fail) == (u32)JIT_EXIT_DONE);
        }
        if (!all_fast)
            continue;  // degenerate draw; slow paths covered in test_mem_slow
        char tag[64];
        snprintf(tag, sizeof(tag), "mem-fast tb=%d n=%u", tb, n);
        diff_mem(pg, plan, ram, 0x1000, 0x2000, init, tag);
    }
    if (g_verbose) printf("[jit-test] mem fast done\n");
}

void test_mem_slow(ExecPage& pg) {
    std::vector<u8> ram(64 * 1024, 0xAA);
    const u32 SZ = (u32)ram.size();
    struct SlowCase { u32 word; u32 rs_val; const char* name; };
    // Fixed slow vectors (rs=1 holds rs_val, rt=2/3).
    const SlowCase cases[] = {
        {w_mem(0x23, 1, 2, 0), 0xC0000000u, "LW KSEG2"},
        {w_mem(0x23, 1, 2, 0), 0xFFFFFFFFu, "LW top"},
        {w_mem(0x23, 1, 2, 0), SZ, "LW KUSEG OOB"},
        {w_mem(0x23, 1, 2, 0), SZ - 3, "LW tail-cross"},
        {w_mem(0x21, 1, 2, 0), SZ - 1, "LH tail-cross"},
        {w_mem(0x20, 1, 2, 0), SZ, "LB OOB"},
        {w_mem(0x23, 1, 2, 0), 0x80100000u, "LW phys>size"},
        {w_mem(0x2B, 1, 2, 0), 0x00001000u, "SW code-start"},
        {w_mem(0x2B, 1, 2, 0), 0x00001FFFu, "SW code-end-1"},
        {w_mem(0x29, 1, 2, 0), 0x00001000u, "SH code"},
        {w_mem(0x28, 1, 2, 0), 0x00001FFFu, "SB code"},
        {w_mem(0x2B, 1, 2, 0), SZ, "SW OOB"},
        // Fast controls (must exit DONE, not SLOW):
        {w_mem(0x23, 1, 2, 0), 0x00000000u, "LW zero"},
        {w_mem(0x23, 1, 2, 0), SZ - 4, "LW last-word"},
        {w_mem(0x20, 1, 2, 0), SZ - 1, "LB last-byte"},
        {w_mem(0x23, 1, 2, 0), 0x00001000u - 4, "LW before-code"},
        {w_mem(0x2B, 1, 2, 0), 0x00001000u - 4, "SW before-code"},
        {w_mem(0x2B, 1, 2, 0), 0x00002000u, "SW code-end"},
        {w_mem(0x23, 1, 2, 0), 0x00001500u, "LW in-code (loads OK)"},
    };
    for (const SlowCase& c : cases) {
        JitTbPlan plan = jit_decode_tb(&c.word, 1);
        MemState init; memset(&init, 0, sizeof(init));
        init.gpr[1] = c.rs_val;
        init.gpr[2] = 0xDEADBEEFu;
        init.gpr[3] = 0x12345678u;
        char tag[96];
        snprintf(tag, sizeof(tag), "mem-slow %s", c.name);
        diff_mem(pg, plan, ram, 0x1000, 0x2000, init, tag);
    }
    // Partial progress: fast, fast, slow -> SLOW with exit_arg=2.
    // Also a read-your-write chain: SW [B] then LW back from [B].
    {
        u32 words[3] = {w_mem(0x2B, 1, 4, 0), w_mem(0x23, 1, 5, 0), w_mem(0x23, 7, 6, 0)};
        JitTbPlan plan = jit_decode_tb(words, 3);
        MemState init; memset(&init, 0, sizeof(init));
        init.gpr[1] = 0x00005000u;     // fast KUSEG base (outside code range)
        init.gpr[4] = 0x11223344u;     // stored by op0, loaded back by op1
        init.gpr[5] = 0x00000000u;
        init.gpr[6] = 0xDEADBEEFu;     // slow op2 must leave this alone
        init.gpr[7] = SZ + 0x100;      // KUSEG OOB -> op2 slow, idx 2
        diff_mem(pg, plan, ram, 0x1000, 0x2000, init, "mem-slow partial");
    }
    // Null mem_base honors the "0 = slow path" contract (first mem op slow).
    {
        u32 words[2] = {w_mem(0x09, 1, 2, 1), w_mem(0x23, 1, 3, 0)};  // addiu, LW
        JitTbPlan plan = jit_decode_tb(words, 2);
        MemState init; memset(&init, 0, sizeof(init));
        init.gpr[1] = 0x80000010u;
        std::vector<u8> ra = ram, rb = ram;
        u32 pages = (u32)(ram.size() + 4095) / 4096;
        std::vector<u32> wca(pages, 0), wcb(pages, 0);
        MemState a = init;
        a.mem_base = 0;  // null -> slow
        a.mem_size = SZ;
        a.code_start = 0x1000; a.code_end = 0x2000;
        a.wc_base = (u64)(uintptr_t)wca.data();
        a.exit_arg = 0xDEADBEEFu;
        JitMemState ref;
        memcpy(ref.regs, init.gpr, sizeof(ref.regs));
        ref.hi = ref.lo = 0;
        ref.ram = nullptr; ref.ram_size = SZ;  // null -> slow
        ref.code_start = 0x1000; ref.code_end = 0x2000;
        ref.wc = wcb.data(); ref.wc_pages = pages;
        u32 ref_fail = 0;
        u32 ref_exit = jit_run_mem_reference(plan, ref, ref_fail);
        u32 exit = run_mem_tb(pg, plan, a);
        CHECK(exit == ref_exit && exit == (u32)JIT_EXIT_SLOW_MEM, "mem nullbase exit");
        CHECK(a.exit_arg == 1 && ref_fail == 1, "mem nullbase idx");
        CHECK(a.gpr[2] == ref.regs[2], "mem nullbase alu-applied");
    }
    if (g_verbose) printf("[jit-test] mem slow done\n");
}

void test_stops() {
    // Decoder stop classification (no exec needed).
    struct Case { u32 word; JitStop stop; const char* name; };
    const Case cases[] = {
        {0xC0000000u, JIT_STOP_MEM, "LL"},
        {0xD0000000u, JIT_STOP_MEM, "SC"},
        {0x88000000u, JIT_STOP_MEM, "LWL"},
        {0x98000000u, JIT_STOP_MEM, "LWR"},
        {0xA8000000u, JIT_STOP_MEM, "SWL"},
        {0xB8000000u, JIT_STOP_MEM, "SWR"},
        {0xBC000000u, JIT_STOP_MEM, "CACHE"},
        {0xC4000000u, JIT_STOP_MEM, "LWC1"},
        {0xC8000000u, JIT_STOP_MEM, "LWC2"},
        {0xD4000000u, JIT_STOP_MEM, "SWC1"},
        {0x40000000u, JIT_STOP_COP, "MFC0"},
        {0x48000000u, JIT_STOP_COP, "COP2"},
        {0x00000008u, JIT_STOP_JR, "JR"},
        {0x00000009u, JIT_STOP_JR, "JALR"},
        {0x0000000Cu, JIT_STOP_TRAP, "SYSCALL"},
        {0x0000000Du, JIT_STOP_TRAP, "BREAK"},
        {0x70000000u, JIT_STOP_SPECIAL2, "MADD"},
        {0x7C000001u, JIT_STOP_SPECIAL3, "SPECIAL3-other"},  // func=1 (not EXT/INS)
        {0x00000004u, JIT_STOP_UNKNOWN, "SLLV"},
    };
    for (const Case& c : cases) {
        JitOpProbe pr = jit_probe_op(c.word);
        CHECK(!pr.valid && pr.stop == c.stop, "stop %s: valid=%d stop=%d want=%d",
              c.name, pr.valid, pr.stop, c.stop);
    }
    // Branch opcodes are now compilable in Phase 2
    CHECK(jit_probe_op(0x08000000u).valid, "J valid");
    CHECK(jit_probe_op(0x0C000000u).valid, "JAL valid");
    CHECK(jit_probe_op(0x10000000u).valid, "BEQ valid");
    CHECK(jit_probe_op(0x04000000u).valid, "REGIMM (BGEZ/BLTZ) valid");
    // MUL decodes valid; EXT/INS decode valid.
    CHECK(jit_probe_op(w_special2_mul(4, 5, 6)).valid, "MUL valid");
    CHECK(jit_probe_op(w_ext(4, 5, 15, 0)).valid, "EXT valid");
    CHECK(jit_probe_op(w_ins(4, 5, 15, 0)).valid, "INS valid");
    CHECK(jit_probe_op(w_special(1, 2, 3, 0, 0x21)).valid, "ADDU valid");
    // Phase 3: fast loads/stores decode valid (LB/LH/LW/LBU/LHU/SB/SH/SW).
    const u32 mem_ops[] = {0x20, 0x21, 0x23, 0x24, 0x25, 0x28, 0x29, 0x2B};
    for (u32 mop : mem_ops) {
        u32 w = (mop << 26) | (3 << 21) | (4 << 16) | 0x10;
        JitOpProbe pr = jit_probe_op(w);
        CHECK(pr.valid, "mem op=0x%02X valid", mop);
    }
    // Loads with rt==0 fold to NOP (still valid); stores with rt==0 stay live
    // (the interpreter stores regs[0]==0 — skipping would diverge).
    {
        JitOpProbe pl = jit_probe_op((0x23u << 26) | (3 << 21) | (0 << 16));
        CHECK(pl.valid && pl.op.op == JIT_ALU_NOP, "LW r0 -> NOP");
        JitOpProbe ps = jit_probe_op((0x2Bu << 26) | (3 << 21) | (0 << 16));
        CHECK(ps.valid && ps.op.op == JIT_ALU_SW, "SW r0 stays live");
    }
    if (g_verbose) printf("[jit-test] stop classification done\n");
}

}  // namespace

JitTestResult jit_run_phase1_tests(bool verbose) {
    printf("[jit-test] enter\n"); fflush(stdout);
    g_verbose = verbose;
    passes = 0; failures = 0;
    ExecPage pg;
    if (!pg.alloc(4096)) {
        printf("[jit-test] exec page alloc FAILED\n");
        failures++;
        return JitTestResult{0, 1};
    }
    printf("[jit-test] page ok, single ops...\n"); fflush(stdout);
    test_single_ops(pg);
    printf("[jit-test] single done, multi...\n"); fflush(stdout);
    test_multi_tb(pg);
    printf("[jit-test] multi done, mem fast...\n"); fflush(stdout);
    test_mem_fast(pg);
    printf("[jit-test] mem fast done, mem slow...\n"); fflush(stdout);
    test_mem_slow(pg);
    printf("[jit-test] mem slow done, stops...\n"); fflush(stdout);
    test_stops();
    // Overflow: tiny buffer must fail cleanly (return 0, no crash).
    {
        u32 w = w_special(2, 3, 4, 0, 0x21);
        JitTbPlan plan = jit_decode_tb(&w, 1);
        u8 tiny[4];
        CHECK(jit_compile_tb(plan, tiny, sizeof(tiny)) == 0, "overflow returns 0");
    }
    pg.done();
    if (verbose || failures)
        printf("[jit-test] passed=%d failed=%d\n", passes, failures);
    return JitTestResult{passes, failures};
}
