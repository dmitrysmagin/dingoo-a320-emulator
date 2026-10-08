#include "emit.h"

#include "../syscalls.h"

#include <cstring>

extern u32 g_cpu_regs[32];
extern u32 g_cpu_pc;
extern u32 g_cpu_hi;
extern u32 g_cpu_lo;

extern "C" u32 jit_got_dispatch(JitState* st, s32 got_index) {
    if (!st || !st->syscalls || got_index < 0)
        return (u32)JIT_EXIT_ERROR;
    memcpy(g_cpu_regs, st->gpr, sizeof(st->gpr));
    g_cpu_hi = st->hi;
    g_cpu_lo = st->lo;
    u32 return_addr = st->gpr[31];
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
