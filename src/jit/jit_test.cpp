#include "jit_test.h"
#include "emit.h"
#include "frontend.h"

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

void test_stops() {
    // Decoder stop classification (no exec needed).
    struct Case { u32 word; JitStop stop; const char* name; };
    const Case cases[] = {
        {0x8C000000u, JIT_STOP_MEM, "LW"},
        {0xAC000000u, JIT_STOP_MEM, "SW"},
        {0xC0000000u, JIT_STOP_MEM, "LL"},
        {0xE0000000u, JIT_STOP_MEM, "SWC1"},
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
    printf("[jit-test] multi done, stops...\n"); fflush(stdout);
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
