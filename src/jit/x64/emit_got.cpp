#include "emit.h"

#include <cstddef>

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
