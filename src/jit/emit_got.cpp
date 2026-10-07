#include "emit.h"

#include "../syscalls.h"

#include <cstddef>

extern u32 g_cpu_regs[32];
extern u32 g_cpu_pc;
extern u32 g_cpu_hi;
extern u32 g_cpu_lo;
#include <cstring>

static void c8(JitEmit& e, u8 v) {
    if (e.len >= e.cap) { e.oom = true; return; }
    e.buf[e.len++] = v;
}

static void c32(JitEmit& e, u32 v) {
    for (u32 i = 0; i < 4; i++) c8(e, (u8)(v >> (i * 8)));
}

static void c64(JitEmit& e, u64 v) {
    for (u32 i = 0; i < 8; i++) c8(e, (u8)(v >> (i * 8)));
}

static void c_begin(JitEmit& e) {
    c8(e, 0x52);
#ifdef _WIN32
    c8(e, 0x48); c8(e, 0x83); c8(e, 0xEC); c8(e, 0x20);
    c8(e, 0x48); c8(e, 0x8B); c8(e, 0xCA);
#else
    c8(e, 0x48); c8(e, 0x8B); c8(e, 0xFA);
#endif
}

static void c_arg2_imm(JitEmit& e, u32 v) {
#ifdef _WIN32
    c8(e, 0xBA); c32(e, v);
#else
    c8(e, 0xBE); c32(e, v);
#endif
}

static void c_call_fn(JitEmit& e, void* fn) {
    c8(e, 0x48); c8(e, 0xB8); c64(e, (u64)(uintptr_t)fn);
    c8(e, 0xFF); c8(e, 0xD0);
#ifdef _WIN32
    c8(e, 0x48); c8(e, 0x83); c8(e, 0xC4); c8(e, 0x20);
#endif
    c8(e, 0x5A);
}

// Phase 6c: mirrors cpu.cpp execute_one_impl GOT block (sync + dispatch + resume).
extern "C" u32 jit_got_dispatch(JitState* st, s32 got_index) {
    if (!st || !st->syscalls || got_index < 0)
        return (u32)JIT_EXIT_ERROR;
    memcpy(g_cpu_regs, st->gpr, sizeof(st->gpr));
    g_cpu_hi = st->hi;
    g_cpu_lo = st->lo;
    u32 return_addr = st->gpr[31];
    // Match cpu.cpp GOT path: scheduler saves resume PC ($ra), not GOT slot.
    g_cpu_pc = return_addr;
    st->syscalls->clear_task_switched();
    st->syscalls->dispatch((int)got_index, return_addr);
    memcpy(st->gpr, g_cpu_regs, sizeof(st->gpr));
    st->hi = g_cpu_hi;
    st->lo = g_cpu_lo;
    u32 npc = st->syscalls->task_switched() ? g_cpu_pc : return_addr;
    st->next_pc = npc;
    if ((npc & 0x80000000u) == 0 && (npc == 0 || npc >= 0x4000u))
        return (u32)JIT_EXIT_ERROR;
    return (u32)JIT_EXIT_GOT;
}

u32 jit_compile_got_tb(u32 entry_pc, s32 got_index, u8* buf, u32 cap) {
    JitEmit e{buf, cap, 0, false};
    // Interpreter GOT path (cpu.cpp): after JAL/JR the guest PC lands on the
    // slot and Syscalls::dispatch runs without executing the stub words there.
    // Only cop0.tick() / insn_count++ happen once for that dispatch.
    jit_emit_prolog(e, entry_pc, 0);
    c8(e, 0x83); c8(e, 0x82); c32(e, JIT_OFF_TICK_DELTA);
    c8(e, 1);
    c_begin(e);
    c_arg2_imm(e, (u32)got_index);
    c_call_fn(e, (void*)jit_got_dispatch);
    c8(e, 0xC3);
    if (e.oom)
        return 0;
    return e.len;
}
