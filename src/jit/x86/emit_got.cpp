#include "emit.h"

#include <cstddef>

static void c8(JitEmit& e, u8 v) {
    if (e.len >= e.cap) { e.oom = true; return; }
    e.buf[e.len++] = v;
}

static void c32(JitEmit& e, u32 v) {
    for (u32 i = 0; i < 4; i++) c8(e, (u8)(v >> (i * 8)));
}

static void c_got_call(JitEmit& e, void* fn, u32 got_index) {
    c8(e, 0x68);
    c32(e, got_index);
    c8(e, 0x56);  // push esi = st
    c8(e, 0xB8);
    c32(e, (u32)(uintptr_t)fn);
    c8(e, 0xFF);
    c8(e, 0xD0);
    c8(e, 0x83);
    c8(e, 0xC4);
    c8(e, 8);
}

u32 jit_compile_got_tb(u32 entry_pc, s32 got_index, u8* buf, u32 cap) {
    JitEmit e{buf, cap, 0, false};
    jit_emit_prolog(e, entry_pc, 0);
    c8(e, 0x83);
    c8(e, 0x86);
    c32(e, JIT_OFF_TICK_DELTA);
    c8(e, 1);
    c_got_call(e, (void*)jit_got_dispatch, (u32)got_index);
    jit_emit_epilog(e, (u32)JIT_EXIT_DONE);
    if (e.oom)
        return 0;
    return e.len;
}
