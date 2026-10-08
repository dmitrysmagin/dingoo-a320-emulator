#include "jit_test.h"
#include "host_config.h"
#include "emit.h"
#include "frontend.h"
#include "../memory.h"
#include "../cop0.h"
#include "../cpu.h"
#include "../syscalls.h"
#include "../display.h"

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


// Tests execute compiled TBs against real JitState layout (Phase 6c fields
// such as insn_delta must not be a truncated mirror — prolog stores there).
using ExecState = JitState;

u32 run_tb(ExecPage& pg, const JitTbPlan& plan, ExecState& st, u32* out_len = nullptr) {
    u8* buf = (u8*)pg.p;
    u32 len = jit_compile_tb(plan, buf, pg.size, (u32)JIT_EXIT_DONE, 0);
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

static ExecState base_regs(Rng& rng) {
    ExecState s;
    memset(&s, 0, sizeof(s));
    for (int r = 0; r < 32; r++) s.gpr[r] = rng.word();
    s.gpr[0] = 0;
    s.hi = rng.word(); s.lo = rng.word();
    s.tick_delta = rng.next();
    return s;
}

void diff_one(ExecPage& pg, const JitTbPlan& plan, ExecState init, const char* tag) {
    ExecState a = init, b = init;
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
    // Tick accounting: DONE path adds exactly plan.count.
    CHECK(a.tick_delta == init.tick_delta + plan.count,
          "%s: tick emit=%u want=%u", tag, a.tick_delta, init.tick_delta + plan.count);
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
        u32 len = jit_compile_tb(plan, buf, pg.size, (u32)JIT_EXIT_DONE, 0);
        printf("[jit-test] nop-tb len=%u bytes:", len); fflush(stdout);
        for (u32 i = 0; i < len && i < 64; i++) { printf(" %02X", buf[i]); fflush(stdout); }
        printf("\n"); fflush(stdout);
        {   // SLL $8,$9,3 probe.
            u32 w2 = w_special(9, 9, 8, 3, 0x00);
            JitTbPlan p2 = jit_decode_tb(&w2, 1);
            u32 l2 = jit_compile_tb(p2, buf, pg.size, (u32)JIT_EXIT_DONE, 0);
            printf("[jit-test] sll-tb len=%u bytes:", l2); fflush(stdout);
            for (u32 i = 0; i < l2 && i < 64; i++) { printf(" %02X", buf[i]); fflush(stdout); }
            printf("\n"); fflush(stdout);
            ExecState s; memset(&s, 0, sizeof(s));
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
            ExecState init; memset(&init, 0, sizeof(init));
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
            ExecState init; memset(&init, 0, sizeof(init));
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
        ExecState init; memset(&init, 0, sizeof(init));
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
        ExecState init; memset(&init, 0, sizeof(init));
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
        ExecState init; memset(&init, 0, sizeof(init));
        for (int r = 0; r < 32; r++) init.gpr[r] = rng.word();
        init.gpr[0] = 0;
        init.hi = rng.word(); init.lo = rng.word();
        diff_one(pg, plan, init, "multi");
    }
    if (g_verbose) printf("[jit-test] multi-op TBs done\n");
}

// ---- Phase 3 memory discharge tests ----

// ---- Phase 3/4 discharge: TBs over RAM and/or COP0/MXU ----

static u32 w_mem(u32 op, u32 rs, u32 rt, u32 imm16) {
    return (op << 26) | (rs << 21) | (rt << 16) | (imm16 & 0xFFFFu);
}

// Seed a COP0/MXU pair deterministically from random GPRs so MFC/MTC/
// custom TBs exercise real state (counts, masks, MXU_EN both ways).
static void seed_cop(COP0& c, MXU& m, const u32 gpr[32]) {
    c.reset();
    m.reset();
    c.regs.count = gpr[7];
    c.regs.compare = gpr[8];
    c.regs.status = gpr[9];
    c.regs.cause = gpr[10] & 0xFFFF0000u;
    c.regs.epc = gpr[11];
    c.regs.wired = gpr[12] & 0x1Fu;
    c.regs.entry_hi = gpr[13];
    c.regs.entry_lo0 = gpr[14];
    c.regs.context = gpr[15];
    for (int i = 0; i < 16; i++) m.state.xregs[i] = gpr[(i * 3 + 1) % 32];
    m.state.acc[0] = gpr[17]; m.state.acc[1] = gpr[18];
    m.state.acc[2] = gpr[19]; m.state.acc[3] = gpr[20];
    m.state.ctrl = gpr[21];  // bit 0 (MXU_EN) random — harmless here
    m.state.p0 = gpr[22]; m.state.p1 = gpr[23]; m.state.p2 = gpr[24];
}

static bool cop_equal(const COP0& a, const COP0& b) {
    return memcmp(&a.regs, &b.regs, sizeof(a.regs)) == 0;
}

static bool mxu_equal(const MXU& a, const MXU& b) {
    return memcmp(&a.state, &b.state, sizeof(a.state)) == 0;
}

// Diff one mem/cop TB: emitted execution (ExecState over ra/wca/ca/ma)
// vs the C++ reference (JitMemState over rb/wcb/cb/mb). All start as copies.
static void diff_mem(ExecPage& pg, const JitTbPlan& plan,
                     const std::vector<u8>& ram_init,
                     u32 code_start, u32 code_end,
                     ExecState init_regs, const char* tag) {
    std::vector<u8> ra = ram_init, rb = ram_init;
    u32 pages = (u32)(ram_init.size() + 4095) / 4096;
    std::vector<u32> wca(pages, 0), wcb(pages, 0);
    COP0 ca, cb;
    MXU ma, mb;
    seed_cop(ca, ma, init_regs.gpr);
    seed_cop(cb, mb, init_regs.gpr);

    ExecState a = init_regs;
    a.mem_base = (u64)(uintptr_t)ra.data();
    a.mem_size = (u32)ra.size();
    a.code_start = code_start;
    a.code_end = code_end;
    a.wc_base = (u64)(uintptr_t)wca.data();
    a.cop0 = &ca;
    a.mxu = &ma;
    a.exit_arg = 0xDEADBEEFu;

    JitMemState ref;
    memcpy(ref.regs, init_regs.gpr, sizeof(ref.regs));
    ref.hi = init_regs.hi; ref.lo = init_regs.lo;
    ref.ram = rb.data(); ref.ram_size = (u32)rb.size();
    ref.code_start = code_start; ref.code_end = code_end;
    ref.wc = wcb.data(); ref.wc_pages = pages;
    ref.cop0 = &cb;
    ref.mxu = &mb;
    u32 ref_tick = init_regs.tick_delta;
    ref.tick_delta = &ref_tick;
    u32 ref_fail = 0xDEADBEEFu;
    u32 ref_exit = jit_run_mem_reference(plan, ref, ref_fail);

    u32 exit = run_tb(pg, plan, a);
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
    CHECK(cop_equal(ca, cb), "%s: COP0 mismatch", tag);
    CHECK(mxu_equal(ma, mb), "%s: MXU mismatch", tag);
    CHECK(a.tick_delta == ref_tick, "%s: tick emit=%u ref=%u",
          tag, a.tick_delta, ref_tick);
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
        ExecState init;
        bool all_fast = false;
        for (int attempt = 0; attempt < 20 && !all_fast; attempt++) {
            for (u32 i = 0; i < n; i++) {
                u32 mop = mem_ops[rng.next() % 8];
                bool is_store = (mop == 0x28 || mop == 0x29 || mop == 0x2B);
                u32 rs = rng.reg(), rt = is_store ? rng.reg() : rng.nzreg();
                words[i] = w_mem(mop, rs, rt, rng.next() & 0xFFFFu);
            }
            plan = jit_decode_tb(words, n);
            init = base_regs(rng);
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
            probe.cop0 = nullptr; probe.mxu = nullptr;  // mem-only plans
            u32 probe_tick = init.tick_delta;
            probe.tick_delta = &probe_tick;
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
        ExecState init; memset(&init, 0, sizeof(init));
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
        ExecState init; memset(&init, 0, sizeof(init));
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
        ExecState init; memset(&init, 0, sizeof(init));
        init.gpr[1] = 0x80000010u;
        std::vector<u8> ra = ram, rb = ram;
        u32 pages = (u32)(ram.size() + 4095) / 4096;
        std::vector<u32> wca(pages, 0), wcb(pages, 0);
        COP0 ca, cb; ca.reset(); cb.reset();
        MXU ma, mb; ma.reset(); mb.reset();
        ExecState a = init;
        a.mem_base = 0;  // null -> slow
        a.mem_size = SZ;
        a.code_start = 0x1000; a.code_end = 0x2000;
        a.wc_base = (u64)(uintptr_t)wca.data();
        a.cop0 = &ca; a.mxu = &ma;
        a.exit_arg = 0xDEADBEEFu;
        JitMemState ref;
        memcpy(ref.regs, init.gpr, sizeof(ref.regs));
        ref.hi = ref.lo = 0;
        ref.ram = nullptr; ref.ram_size = SZ;  // null -> slow
        ref.code_start = 0x1000; ref.code_end = 0x2000;
        ref.wc = wcb.data(); ref.wc_pages = pages;
        ref.cop0 = &cb; ref.mxu = &mb;
        u32 ref_tick = 0;
        ref.tick_delta = &ref_tick;
        u32 ref_fail = 0;
        u32 ref_exit = jit_run_mem_reference(plan, ref, ref_fail);
        u32 exit = run_tb(pg, plan, a);
        CHECK(exit == ref_exit && exit == (u32)JIT_EXIT_SLOW_MEM, "mem nullbase exit");
        CHECK(a.exit_arg == 1 && ref_fail == 1, "mem nullbase idx");
        CHECK(a.gpr[2] == ref.regs[2], "mem nullbase alu-applied");
        CHECK(a.tick_delta == ref_tick && ref_tick == 1, "mem nullbase tick");
    }
    if (g_verbose) printf("[jit-test] mem slow done\n");
}

void test_cop(ExecPage& pg) {
    // Randomized COP0/COP2/MXU TBs (1-5 ops) diffed against the reference.
    // RAM is present but untouched by COP ops; COP0/MXU/tick are compared.
    Rng rng; rng.s = 0xC0DECAFEu;
    std::vector<u8> ram(64 * 1024, 0x55);
    const u32 cop0_mfc_rds[] = {0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15, 16, 26, 27, 28, 30};
    const u32 cop0_mtc_rds[] = {0, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 16};
    const u32 customs[] = {0x01, 0x03, 0x08, 0x09, 0x0B, 0x11, 0x14, 0x19, 0x1B, 0x1E};
    auto w_cop0 = [](u32 rs, u32 rt, u32 rd, u32 func) {
        return (0x10u << 26) | ((rs & 0x1Fu) << 21) | ((rt & 0x1Fu) << 16) |
               ((rd & 0x1Fu) << 11) | (func & 0x3Fu);
    };
    auto w_cop2 = [](u32 rs, u32 rt, u32 rd) {
        return (0x12u << 26) | ((rs & 0x1Fu) << 21) | ((rt & 0x1Fu) << 16) |
               ((rd & 0x1Fu) << 11);
    };
    auto w_custom = [](u32 rs, u32 rt, u32 rd, u32 sa, u32 func) {
        return (0x12u << 26) | ((rs & 0x1Fu) << 21) | ((rt & 0x1Fu) << 16) |
               ((rd & 0x1Fu) << 11) | ((sa & 0x1Fu) << 6) | (func & 0x3Fu);
    };
    auto w_mxu1 = [](u32 rs, u32 rt, u32 rd, u32 func) {
        return (0x1Cu << 26) | ((rs & 0x1Fu) << 21) | ((rt & 0x1Fu) << 16) |
               ((rd & 0x1Fu) << 11) | (func & 0x3Fu);
    };
    const u32 custom_rs[] = {1, 3, 5, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    for (int tb = 0; tb < 100; tb++) {
        u32 n = 1 + rng.next() % 5;
        u32 words[6];
        for (u32 i = 0; i < n; i++) {
            u32 pick = rng.next() % 10;
            u32 rt = (rng.next() & 7) == 0 ? 0 : rng.nzreg();  // sometimes $0
            switch (pick) {
            case 0: words[i] = w_cop0(0, rt, cop0_mfc_rds[rng.next() % 20], 0); break;
            case 1: words[i] = w_cop0(4, rng.reg(), cop0_mtc_rds[rng.next() % 14], 0); break;
            case 2: words[i] = w_cop2(0, rt, rng.next() % 28); break;  // MFC2 (0-27)
            case 3: words[i] = w_cop2(4, rng.reg(), rng.next() % 28); break;  // MTC2
            case 4: words[i] = w_cop2(2, rt, rng.next() % 4); break;  // CFC2
            case 5: words[i] = w_cop2(6, rng.reg(), rng.next() % 4); break;  // CTC2
            case 6: case 7: {  // custom (xreg indices < 16; the impl is unmasked)
                u32 rs = custom_rs[rng.next() % 12];
                words[i] = w_custom(rs, rng.next() % 16, rng.next() % 16,
                                    rng.next() % 32, customs[rng.next() % 10]);
                break;
            }
            case 8: words[i] = w_mxu1(rng.next() % 16, rng.reg(), rng.next() % 16, 0x2E); break;
            default: words[i] = w_mxu1(rng.next() % 16, rng.reg(), rng.next() % 16, 0x2F); break;
            }
        }
        JitTbPlan plan = jit_decode_tb(words, n);
        ExecState init = base_regs(rng);
        char tag[64];
        snprintf(tag, sizeof(tag), "cop tb=%d n=%u", tb, n);
        diff_mem(pg, plan, ram, 0x1000, 0x2000, init, tag);
    }
    // Unknown-register paths (each prints once per side; lock no-crash).
    {
        u32 words[3] = {
            w_cop0(0, 3, 7, 0),    // MFC0 unknown rd=7 -> 0
            w_cop0(4, 3, 15, 0),   // MTC0 unknown rd=15 -> ignored
            w_custom(8, 1, 2, 0, 0x00),  // custom unknown func -> ignored
        };
        JitTbPlan plan = jit_decode_tb(words, 3);
        ExecState init = base_regs(rng);
        diff_mem(pg, plan, ram, 0x1000, 0x2000, init, "cop unknown");
    }
    // MFC to $0 mirrors the interpreter (writes regs[0], r0 corruption and all).
    {
        u32 words[2] = {w_cop0(0, 0, 9, 0), w_cop2(0, 0, 5)};
        JitTbPlan plan = jit_decode_tb(words, 2);
        ExecState init = base_regs(rng);
        diff_mem(pg, plan, ram, 0x1000, 0x2000, init, "cop mfc-r0");
    }
    if (g_verbose) printf("[jit-test] cop done\n");
}

void test_mixed(ExecPage& pg) {
    // Mixed ALU + mem + COP TBs (fast RAM): the full reference path.
    Rng rng; rng.s = 0x5EED1234u;
    std::vector<u8> ram(64 * 1024, 0x33);
    for (int tb = 0; tb < 60; tb++) {
        u32 n = 2 + rng.next() % 5;
        u32 words[7];
        for (u32 i = 0; i < n; i++) {
            u32 pick = rng.next() % 12;
            if (pick < 5) {  // ALU
                u32 rs = rng.nzreg(), rt = rng.nzreg(), rd = rng.nzreg();
                const u32 f[] = {0x21, 0x25, 0x2B, 0x00, 0x24};
                words[i] = (rs << 21) | (rt << 16) | (rd << 11) | f[pick];
                if (pick == 3) words[i] |= (rng.next() % 32) << 6;  // SLL sa
            } else if (pick < 9) {  // mem
                const u32 mop[] = {0x20, 0x23, 0x24, 0x28, 0x2B};
                words[i] = w_mem(mop[pick - 5], rng.reg(), rng.nzreg(), rng.next() & 0xFFFFu);
            } else if (pick == 9) {  // MFC0 count
                words[i] = (0x10u << 26) | (rng.nzreg() << 16) | (9 << 11);
            } else if (pick == 10) {  // MTC2
                words[i] = (0x12u << 26) | (4 << 21) | (rng.reg() << 16) | ((rng.next() % 16) << 11);
            } else {  // custom music-mixer-ish (func 0x01 MAC)
                words[i] = (0x12u << 26) | (8 << 21) | ((rng.next() % 16) << 16) |
                           ((rng.next() % 16) << 11) | 0x01;
            }
        }
        JitTbPlan plan = jit_decode_tb(words, n);
        ExecState init = base_regs(rng);
        for (u32 i = 0; i < n; i++) {
            u32 rs = plan.ops[i].rs;
            if (rs != 0 && (rng.next() & 1))
                init.gpr[rs] = 0x80000000u + (rng.next() % (u32)ram.size());
        }
        // Retry unless all-fast (mem addrs may miss).
        bool all_fast = false;
        for (int attempt = 0; attempt < 10 && !all_fast; attempt++) {
            JitMemState probe;
            memcpy(probe.regs, init.gpr, sizeof(probe.regs));
            probe.hi = init.hi; probe.lo = init.lo;
            std::vector<u8> scratch = ram;
            probe.ram = scratch.data(); probe.ram_size = (u32)scratch.size();
            probe.code_start = 0x1000; probe.code_end = 0x2000;
            std::vector<u32> wcs((ram.size() + 4095) / 4096, 0);
            probe.wc = wcs.data(); probe.wc_pages = (u32)wcs.size();
            COP0 pc0; pc0.reset(); MXU pmx; pmx.reset();
            probe.cop0 = &pc0; probe.mxu = &pmx;
            u32 pt = init.tick_delta;
            probe.tick_delta = &pt;
            u32 fail = 0;
            all_fast = (jit_run_mem_reference(plan, probe, fail) == (u32)JIT_EXIT_DONE);
            if (!all_fast) init = base_regs(rng);
        }
        if (!all_fast)
            continue;
        char tag[64];
        snprintf(tag, sizeof(tag), "mixed tb=%d n=%u", tb, n);
        diff_mem(pg, plan, ram, 0x1000, 0x2000, init, tag);
    }
    if (g_verbose) printf("[jit-test] mixed done\n");
}

// Expected outcome per cpu.cpp execute() semantics.
static bool branch_taken(const JitAluInsn& br, const u32 regs[32]) {
    switch (br.op) {
    case JIT_ALU_J: case JIT_ALU_JAL: return true;
    case JIT_ALU_BEQ: case JIT_ALU_BEQL: return regs[br.rs] == regs[br.rt];
    case JIT_ALU_BNE: case JIT_ALU_BNEL: return regs[br.rs] != regs[br.rt];
    case JIT_ALU_BLEZ: case JIT_ALU_BLEZL: return (s32)regs[br.rs] <= 0;
    case JIT_ALU_BGTZ: case JIT_ALU_BGTZL: return (s32)regs[br.rs] > 0;
    case JIT_ALU_BLTZ: case JIT_ALU_BLTZAL: return (s32)regs[br.rs] < 0;
    default: return (s32)regs[br.rs] >= 0;  // BGEZ/BGEZAL
    }
}

static u32 branch_target(const JitAluInsn& br, u32 branch_pc) {
    if (br.op == JIT_ALU_J || br.op == JIT_ALU_JAL)
        return ((br.uimm & 0x03FFFFFFu) << 2) | ((branch_pc + 4) & 0xF0000000u);
    return (u32)((s32)branch_pc + 4 + (br.imm << 2));
}

static bool branch_likely(JitAluOp op) {
    return op == JIT_ALU_BEQL || op == JIT_ALU_BNEL ||
           op == JIT_ALU_BLEZL || op == JIT_ALU_BGTZL;
}

static bool branch_link(JitAluOp op) {
    return op == JIT_ALU_JAL || op == JIT_ALU_BLTZAL || op == JIT_ALU_BGEZAL;
}

static void diff_branch(ExecPage& pg, const JitTbPlan& plan, u32 branch_idx,
                        u32 entry_pc, ExecState init, const char* tag) {
    // Reference mirrors cpu order: cond from pre-delay regs, link, delay
    // (unless likely-not-taken), then target selection.
    ExecState b = init;
    for (u32 i = 0; i < branch_idx; i++)
        jit_apply_alu(plan.ops[i], b.gpr, b.hi, b.lo);
    const JitAluInsn& br = plan.ops[branch_idx];
    u32 branch_pc = entry_pc + branch_idx * 4;
    bool taken = branch_taken(br, b.gpr);
    u32 want_next = taken ? branch_target(br, branch_pc) : branch_pc + 8;
    if (branch_link(br.op)) b.gpr[31] = branch_pc + 8;
    if (!branch_likely(br.op) || taken)
        jit_apply_alu(plan.ops[branch_idx + 1], b.gpr, b.hi, b.lo);
    u32 want_tick = init.tick_delta + branch_idx + 1;

    ExecState a = init;
    u8* buf = (u8*)pg.p;
    u32 len = jit_compile_branch_tb(plan, branch_idx, entry_pc, buf, pg.size);
    CHECK(len != 0, "%s: compiled", tag);
    if (!len)
        return;
#ifdef _WIN32
    FlushInstructionCache(GetCurrentProcess(), buf, len);
#else
    __builtin___clear_cache((char*)buf, (char*)buf + len);
#endif
    TbFunc fn = (TbFunc)buf;
    u32 exit = fn((JitState*)&a);
    CHECK(exit == (u32)JIT_EXIT_NEXT_PC, "%s: exit=%u", tag, exit);
    if (exit != (u32)JIT_EXIT_NEXT_PC)
        return;
    CHECK(a.next_pc == want_next, "%s: next emit=0x%08X want=0x%08X", tag, a.next_pc, want_next);
    for (int r = 0; r < 32; r++)
        CHECK(a.gpr[r] == b.gpr[r], "%s: r%d emit=0x%08X ref=0x%08X",
              tag, r, a.gpr[r], b.gpr[r]);
    CHECK(a.hi == b.hi && a.lo == b.lo, "%s: hilo", tag);
    CHECK(a.tick_delta == want_tick, "%s: tick emit=%u want=%u", tag, a.tick_delta, want_tick);
}

void test_branch(ExecPage& pg) {
    Rng rng; rng.s = 0x8BACC123u;
    // (kind, word-builder-tag). Words built per-iteration below.
    const JitAluOp kinds[] = {JIT_ALU_J, JIT_ALU_JAL,
        JIT_ALU_BEQ, JIT_ALU_BNE, JIT_ALU_BLEZ, JIT_ALU_BGTZ,
        JIT_ALU_BLTZ, JIT_ALU_BGEZ, JIT_ALU_BLTZAL, JIT_ALU_BGEZAL,
        JIT_ALU_BEQL, JIT_ALU_BNEL, JIT_ALU_BLEZL, JIT_ALU_BGTZL};
    auto w_j = [](u32 op26, u32 tgt26) { return (op26 << 26) | (tgt26 & 0x03FFFFFFu); };
    auto w_br = [](u32 op26, u32 rs, u32 rt, u32 imm) {
        return (op26 << 26) | ((rs & 0x1Fu) << 21) | ((rt & 0x1Fu) << 16) | (imm & 0xFFFFu);
    };
    auto w_regimm = [](u32 rtfield, u32 rs, u32 imm) {
        return (0x01u << 26) | ((rs & 0x1Fu) << 21) | ((rtfield & 0x1Fu) << 16) | (imm & 0xFFFFu);
    };
    int tb = 0;
    for (JitAluOp kind : kinds) {
        for (int force = -1; force <= 1; force++) {  // -1 random, 0 bias not-taken, 1 bias taken
            for (int rep = 0; rep < 4; rep++, tb++) {
                u32 rs = rng.nzreg(), rt = rng.nzreg();
                u32 word = 0;
                // Bias the outcome: same index forces taken for EQ (a reg
                // always equals itself); different indices bias BNE taken.
                // Other kinds rely on random values (both directions appear
                // over reps); diff_branch verifies actual outcome either way.
                switch (kind) {
                case JIT_ALU_J: word = w_j(0x02, rng.next() & 0x03FFFFFFu); break;
                case JIT_ALU_JAL: word = w_j(0x03, rng.next() & 0x03FFFFFFu); break;
                case JIT_ALU_BEQ: case JIT_ALU_BEQL:
                    if (force > 0) rt = rs;
                    else if (force == 0 && rt == rs) rt = (u32)(rs % 31) + 1;
                    word = w_br(0x04, rs, rt, rng.next() & 0xFFFFu);
                    break;
                case JIT_ALU_BNE: case JIT_ALU_BNEL:
                    if (force > 0 && rt == rs) rt = (u32)(rs % 31) + 1;
                    word = w_br(0x05, rs, rt, rng.next() & 0xFFFFu);
                    break;
                case JIT_ALU_BLEZ: case JIT_ALU_BLEZL:
                    word = w_br(0x06, rs, 0, rng.next() & 0xFFFFu); break;
                case JIT_ALU_BGTZ: case JIT_ALU_BGTZL:
                    word = w_br(0x07, rs, 0, rng.next() & 0xFFFFu); break;
                case JIT_ALU_BLTZ: word = w_regimm(0, rs, rng.next() & 0xFFFFu); break;
                case JIT_ALU_BGEZ: word = w_regimm(1, rs, rng.next() & 0xFFFFu); break;
                case JIT_ALU_BLTZAL: word = w_regimm(16, rs, rng.next() & 0xFFFFu); break;
                default: word = w_regimm(17, rs, rng.next() & 0xFFFFu); break;  // BGEZAL
                }
                // Prefix 0-2 ALU ops + delay op (sometimes clobbering rs/rt/ra).
                u32 nprefix = (u32)(rng.next() % 3);
                u32 words[8];
                for (u32 i = 0; i < nprefix; i++)
                    words[i] = w_special(rng.nzreg(), rng.nzreg(), rng.nzreg(), rng.next() % 32, 0x21);
                words[nprefix] = word;
                u32 dchoice = rng.next() % 6;
                u32 drd = (dchoice < 2) ? rs : (dchoice < 4) ? rt : (dchoice == 4 ? 31 : rng.nzreg());
                u32 d_op = w_special(rng.nzreg(), rng.nzreg(), drd, rng.next() % 32,
                                     dchoice == 5 ? 0x25 : 0x21);
                words[nprefix + 1] = d_op;
                u32 n = nprefix + 2;
                JitTbPlan plan = jit_decode_tb(words, n);
                // Plan must hold prefix + branch + delay exactly.
                if (plan.count != n)
                    continue;  // degenerate (e.g. rd==0 NOP is fine — still counts)
                ExecState init; memset(&init, 0, sizeof(init));
                for (int r = 0; r < 32; r++) init.gpr[r] = rng.word();
                init.gpr[0] = 0;
                init.hi = rng.word(); init.lo = rng.word();
                init.tick_delta = rng.next();
                u32 entry_pc = 0x80A00000u + (rng.next() % 0x10000u & ~3u);
                if ((tb & 15) == 0) entry_pc = 0xA0000000u + (rng.next() % 0x10000u & ~3u);
                char tag[96];
                snprintf(tag, sizeof(tag), "branch tb=%d kind=%d", tb, (int)kind);
                diff_branch(pg, plan, nprefix, entry_pc, init, tag);
            }
        }
    }
    // Segment-edge J: branch at 0x8FFFFFFC uses (A+4) high bits like cpu.cpp.
    {
        u32 words[2] = {w_j(0x02, 0x123456u), w_special(1, 2, 3, 0, 0x21)};
        JitTbPlan plan = jit_decode_tb(words, 2);
        ExecState init; memset(&init, 0, sizeof(init));
        init.gpr[1] = 0x11111111u; init.gpr[2] = 0x22222222u;
        diff_branch(pg, plan, 0, 0x8FFFFFFCu, init, "branch segedge");
    }
    // KSEG1 JAL link + target high bits.
    {
        u32 words[2] = {w_j(0x03, 0x654321u), 0x00000000u};
        JitTbPlan plan = jit_decode_tb(words, 2);
        ExecState init; memset(&init, 0, sizeof(init));
        diff_branch(pg, plan, 0, 0xA0A01000u, init, "branch kseg1jal");
    }
    if (g_verbose) printf("[jit-test] branch done\n");
}

void test_formation() {
    // Formation against a real Memory (no CPU needed).
    Memory mem;
    auto put = [&](u32 vaddr, u32 w) { mem.write_u32(vaddr, w); };
    u32 alu = w_special(2, 3, 4, 0, 0x21);  // addu r4,r2,r3
    u32 beq = (0x04u << 26) | (2 << 21) | (3 << 16) | 0x10;
    u32 j = (0x02u << 26) | 0x123456u;
    u32 lw = w_mem(0x23, 1, 2, 0);
    const u32 B = 0x80A01000u;  // above the OS-area guard (< 0x80A00000)
    // 1. straight run (JR terminates; zero RAM would be a NOP slide).
    for (u32 i = 0; i < 5; i++) put(B + i * 4, alu);
    put(B + 20, 0x03E00008u);  // JR $ra
    {
        JitFormed f = jit_form_tb(&mem, B, 0, 0);
        CHECK(f.n == 5 && !f.has_branch, "form straight n=%u br=%d", f.n, f.has_branch);
    }
    // 2. branch + delay included, trailing op excluded.
    put(B + 8, beq);
    put(B + 12, alu);
    put(B + 16, alu);
    {
        JitFormed f = jit_form_tb(&mem, B, 0, 0);
        CHECK(f.n == 4 && f.has_branch && f.branch_idx == 2,
              "form branch n=%u br=%d idx=%u", f.n, f.has_branch, f.branch_idx);
    }
    // 3. mem delay slot -> truncate before branch.
    put(B + 12, lw);
    {
        JitFormed f = jit_form_tb(&mem, B, 0, 0);
        CHECK(f.n == 2 && !f.has_branch, "form memdelay n=%u", f.n);
    }
    put(B + 12, alu);
    // 4. branch delay slot -> truncate.
    put(B + 12, beq);
    {
        JitFormed f = jit_form_tb(&mem, B, 0, 0);
        CHECK(f.n == 2 && !f.has_branch, "form brdelay n=%u", f.n);
    }
    put(B + 12, alu);
    // 5. stop PC at delay slot -> truncate before branch.
    {
        JitFormed f = jit_form_tb(&mem, B, B + 12, 0);
        CHECK(f.n == 2 && !f.has_branch, "form stopdelay n=%u", f.n);
    }
    // 6. GOT range stops normal formation; GOT head may form (Phase 6c).
    mem.set_got_range(B + 16, 2);
    put(B + 16, alu);
    put(B + 20, alu);
    {
        JitFormed f = jit_form_tb(&mem, B, 0, 0);
        CHECK(f.n == 4 && f.has_branch, "form got n=%u", f.n);
        JitFormed g = jit_form_got_at(&mem, B + 16);
        CHECK(g.n == 2 && g.is_got, "form gothead n=%u", g.n);
    }
    mem.set_got_range(0, 0);
    // 7. KUSEG / OS-area PCs never form.
    CHECK(jit_form_tb(&mem, 0x00001000u, 0, 0).n == 0, "form kuseg");
    CHECK(jit_form_tb(&mem, 0x80000000u, 0, 0).n == 0, "form osarea");
    // 8. J at head with valid delay.
    put(B + 20, j);
    put(B + 24, alu);
    {
        JitFormed f = jit_form_tb(&mem, B + 20, 0, 0);
        CHECK(f.n == 2 && f.has_branch && f.branch_idx == 0, "form jhead");
    }
    // 9. cap at 64.
    for (u32 i = 0; i < 70; i++) put(B + 0x100 + i * 4, alu);
    {
        JitFormed f = jit_form_tb(&mem, B + 0x100, 0, 0);
        CHECK(f.n == 64 && !f.has_branch, "form cap n=%u", f.n);
    }
    // 10. ERET delay slot truncates (ERET never validates).
    put(B + 12, 0x42000018u);
    {
        JitFormed f = jit_form_tb(&mem, B, 0, 0);
        CHECK(f.n == 2 && !f.has_branch, "form eretdelay n=%u", f.n);
    }
    if (g_verbose) printf("[jit-test] formation done\n");
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
        {0x44000000u, JIT_STOP_COP, "COP1"},
        {0x4C000000u, JIT_STOP_COP, "COP3"},
        {0x42000002u, JIT_STOP_COP, "TLBWI"},
        {0x42000008u, JIT_STOP_COP, "TLBP"},
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
    // Phase 4: COP0/COP2 decode (mirrors cpu.cpp, quirks included).
    {
        // MFC0/MTC0 valid (any func — interpreter ignores it).
        JitOpProbe mfc = jit_probe_op((0x10u << 26) | (0 << 21) | (2 << 16) | (9 << 11));
        CHECK(mfc.valid && mfc.op.op == JIT_COP_MFC0, "MFC0 valid");
        JitOpProbe mtc = jit_probe_op((0x10u << 26) | (4 << 21) | (3 << 16) | (12 << 11));
        CHECK(mtc.valid && mtc.op.op == JIT_COP_MTC0, "MTC0 valid");
        // WAIT traps in the interpreter (COP0/C0 func 0x20 hits the C0
        // default arm -> EXC_RI), so it decodes STOP_COP like other C0 traps.
        JitOpProbe wait = jit_probe_op(0x42000020u);
        CHECK(!wait.valid && wait.stop == JIT_STOP_COP, "WAIT traps");
        // MFC0/MFC2 to $0 stay valid (interpreter writes regs[0]; mirrored).
        JitOpProbe mfc0 = jit_probe_op((0x10u << 26) | (0 << 21) | (0 << 16) | (9 << 11));
        CHECK(mfc0.valid && mfc0.op.op == JIT_COP_MFC0, "MFC0 r0 valid");
        // COP0 rs=2/6 (no such move): NOP, still valid.
        JitOpProbe nop0 = jit_probe_op((0x10u << 26) | (2 << 21));
        CHECK(nop0.valid && nop0.op.op == JIT_ALU_NOP, "COP0 rs=2 NOP");
        // ERET ends the TB with STOP_ERET.
        JitOpProbe eret = jit_probe_op(0x42000018u);
        CHECK(!eret.valid && eret.stop == JIT_STOP_ERET, "ERET stop");
        // COP2 moves + custom valid.
        CHECK(jit_probe_op((0x12u << 26)).valid, "MFC2 valid");
        CHECK(jit_probe_op((0x12u << 26) | (4 << 21)).valid, "MTC2 valid");
        CHECK(jit_probe_op((0x12u << 26) | (2 << 21)).valid, "CFC2 valid");
        CHECK(jit_probe_op((0x12u << 26) | (6 << 21)).valid, "CTC2 valid");
        JitOpProbe cust = jit_probe_op((0x12u << 26) | (8 << 21) | 0x01);
        CHECK(cust.valid && cust.op.op == JIT_COP_CUSTOM, "COP2 custom valid");
        // SPECIAL2 S32M2I/S32I2M valid (unconditional in cpu.cpp); MADD stays slow.
        JitOpProbe m2i = jit_probe_op((0x1Cu << 26) | 0x2E);
        CHECK(m2i.valid && m2i.op.op == JIT_COP_MXU1, "S32M2I valid");
        JitOpProbe i2m = jit_probe_op((0x1Cu << 26) | 0x2F);
        CHECK(i2m.valid && i2m.op.op == JIT_COP_MXU1, "S32I2M valid");
        CHECK(!jit_probe_op(0x70000004u).valid, "MSUB stays slow");
    }
    if (g_verbose) printf("[jit-test] stop classification done\n");
}

void test_cop0_tick_batch() {
    Rng rng;
    rng.s = 0xABCDEF01u;
    for (int t = 0; t < 5000; t++) {
        COP0 a, b;
        a.reset();
        b.reset();
        a.regs.random = rng.next() & 31u;
        b.regs.random = a.regs.random;
        u32 n = (rng.next() % 200u) + 1u;
        a.flush_ticks(n, true);
        for (u32 i = 0; i < n; i++)
            b.tick();
        CHECK(a.regs.count == b.regs.count, "tick batch count wired0");
        CHECK(a.regs.random == b.regs.random, "tick batch random wired0");
    }
    COP0 a, b;
    a.reset();
    b.reset();
    a.regs.wired = 5;
    b.regs.wired = 5;
    a.regs.random = 20;
    b.regs.random = 20;
    u32 n = 50;
    a.flush_ticks(n, true);
    for (u32 i = 0; i < n; i++)
        b.tick();
    CHECK(a.regs.count == b.regs.count, "tick batch count wired>0");
    CHECK(a.regs.random == b.regs.random, "tick batch random wired>0");
}

#if defined(JIT_HOST_ARM64)
void test_stub_host_codegen() {
    printf("[jit-test] stub host %s codegen...\n", JIT_HOST_NAME); fflush(stdout);
    u32 w = w_special(9, 9, 8, 3, 0x00);
    JitTbPlan plan = jit_decode_tb(&w, 1);
    u8 buf[256];
    CHECK(jit_compile_tb(plan, buf, sizeof(buf), (u32)JIT_EXIT_DONE, 0) == 0,
          "stub compile_tb returns 0");
    CHECK(jit_compile_branch_tb(plan, 0, 0, buf, sizeof(buf), nullptr) == 0,
          "stub compile_branch_tb returns 0");
    CHECK(jit_compile_got_tb(0x80001000u, 0, buf, sizeof(buf)) == 0,
          "stub compile_got_tb returns 0");
}

void test_reference_smoke() {
    printf("[jit-test] reference smoke (no discharge)...\n"); fflush(stdout);
    Rng rng; rng.s = 0xABCDEF01u;
    for (int iter = 0; iter < 64; iter++) {
        u32 w = w_special(rng.reg(), rng.reg(), rng.nzreg(), rng.sa(), 0x21);
        JitTbPlan plan = jit_decode_tb(&w, 1);
        if (plan.count != 1)
            continue;
        ExecState init = base_regs(rng);
        ExecState ref = init;
        u32 hi = init.hi, lo = init.lo;
        jit_run_reference(plan, ref.gpr, &hi, &lo);
        CHECK(hi == init.hi || plan.ops[0].op == JIT_ALU_MTHI, "ref hi ok");
        (void)lo;
    }
}
#endif

}  // namespace

JitTestResult jit_run_phase1_tests(bool verbose) {
    printf("[jit-test] enter host=%s\n", JIT_HOST_NAME); fflush(stdout);
    g_verbose = verbose;
    passes = 0; failures = 0;
#if defined(JIT_HOST_X64) || defined(JIT_HOST_X86)
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
    printf("[jit-test] mem slow done, cop...\n"); fflush(stdout);
    test_cop(pg);
    printf("[jit-test] cop done, mixed...\n"); fflush(stdout);
    test_mixed(pg);
    printf("[jit-test] mixed done, branch...\n"); fflush(stdout);
    test_branch(pg);
    printf("[jit-test] branch done, formation...\n"); fflush(stdout);
#else
    test_stub_host_codegen();
    test_reference_smoke();
    printf("[jit-test] stub checks done, formation...\n"); fflush(stdout);
#endif
    test_formation();
    printf("[jit-test] formation done, stops...\n"); fflush(stdout);
    test_stops();
    test_cop0_tick_batch();
#if defined(JIT_HOST_X64) || defined(JIT_HOST_X86)
    {
        u32 w = w_special(2, 3, 4, 0, 0x21);
        JitTbPlan plan = jit_decode_tb(&w, 1);
        u8 tiny[4];
        CHECK(jit_compile_tb(plan, tiny, sizeof(tiny), (u32)JIT_EXIT_DONE, 0) == 0,
              "overflow returns 0");
    }
    pg.done();
#endif
    if (verbose || failures)
        printf("[jit-test] passed=%d failed=%d (host=%s)\n", passes, failures,
               JIT_HOST_NAME);
    return JitTestResult{passes, failures};
}
