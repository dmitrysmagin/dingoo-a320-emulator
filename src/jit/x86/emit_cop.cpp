#include "emit.h"

#include <cstddef>

static_assert(offsetof(JitState, cop0) == (size_t)JIT_OFF_COP0_PTR, "cop0 moved");
static_assert(offsetof(JitState, mxu) == (size_t)JIT_OFF_MXU_PTR, "mxu moved");
static_assert(offsetof(JitState, tick_delta) == (size_t)JIT_OFF_TICK_DELTA, "tick moved");
static_assert(sizeof(JitState) >= 208, "JitState size changed");

// Phase 4 COP0/COP2/MXU: calls into the existing cop0.cpp/mxu.cpp
// implementations (correctness, not speed). The emitter only marshals
// (JitState*, reg, value/insn); guest GPRs stay resident in JitState
// memory so no flush/reload protocol is needed around calls.
//
// IA-32 __cdecl helpers: push args right-to-left, call, caller pops stack.
// ESI holds JitState* (see emit_alu prolog); push esi around each call.

// ---- C-linkage helpers: thin unwraps over cop0.cpp/mxu.cpp ----

extern "C" u32 jit_cop0_mfc(JitState* st, u32 rd) {
    return st->cop0->mfc0((int)rd);
}

extern "C" void jit_cop0_mtc(JitState* st, u32 rd, u32 val) {
    st->cop0->mtc0((int)rd, val);
}

extern "C" u32 jit_cop2_mfc(JitState* st, u32 fs) {
    return st->mxu->mfc2((int)fs);
}

extern "C" void jit_cop2_mtc(JitState* st, u32 fs, u32 val) {
    st->mxu->mtc2((int)fs, val);
}

extern "C" u32 jit_cop2_cfc(JitState* st, u32 fs) {
    return st->mxu->cfc2((int)fs);
}

extern "C" void jit_cop2_ctc(JitState* st, u32 fs, u32 val) {
    st->mxu->ctc2((int)fs, val);
}

extern "C" void jit_mxu_custom(JitState* st, u32 insn) {
    st->mxu->exec_custom(insn);
}

// S32M2I/S32I2M (SPECIAL2 0x2E/0x2F): verbatim replica of the cpu.cpp
// exec_special2 cases, including the unconditional regs[rt] write (even
// rt==0 — the interpreter does the same, so the JIT mirrors it).
extern "C" void jit_mxu1_mov(JitState* st, u32 insn) {
    MXU* m = st->mxu;
    u32 rt = (insn >> 16) & 0x1F;
    u32 rd = (insn >> 11) & 0x1F;
    if ((insn & 0x3F) == 0x2E)
        st->gpr[rt] = m->state.xregs[rd & 15];
    else
        m->state.xregs[rd & 15] = st->gpr[rt];
}

// ---- raw call emission ----

static void c8(JitEmit& e, u8 v) {
    if (e.len >= e.cap) { e.oom = true; return; }
    e.buf[e.len++] = v;
}

static void c32(JitEmit& e, u32 v) {
    for (u32 i = 0; i < 4; i++) c8(e, (u8)(v >> (i * 8)));
}

static void c_invoke(JitEmit& e, void* fn, u32 stack_pop) {
    c8(e, 0xB8);
    c32(e, (u32)(uintptr_t)fn);
    c8(e, 0xFF);
    c8(e, 0xD0);
    c8(e, 0x83);
    c8(e, 0xC4);
    c8(e, (u8)stack_pop);
}

static void c_call1(JitEmit& e, void* fn, u32 a) {
    // jit_*(JitState* st, u32 reg): cdecl, ESI preserved across calls.
    c8(e, 0x68);
    c32(e, a);
    c8(e, 0x56);  // push esi = st
    c_invoke(e, fn, 8);
}

static void c_call2(JitEmit& e, void* fn, u32 a) {
    c8(e, 0x50);  // push eax (val, 3rd param)
    c8(e, 0x68);
    c32(e, a);
    c8(e, 0x56);  // push esi = st
    c_invoke(e, fn, 12);
}

// mov r32, [RDX+disp32] / mov [RDX+disp32], eax (mod=10, no SIB).
static void c_load_eax(JitEmit& e, u32 off) {
    c8(e, 0x8B); c8(e, 0x86); c32(e, off);
}

static void c_store_eax(JitEmit& e, u32 off) {
    c8(e, 0x89); c8(e, 0x86); c32(e, off);
}

bool emit_cop_op(JitEmit& e, const JitAluInsn& o) {
    switch (o.op) {
    case JIT_COP_MFC0:
        c_call1(e, (void*)jit_cop0_mfc, o.rd);
        c_store_eax(e, slot_off(o.rt));  // unconditional (mirrors cpu.cpp)
        return !e.oom;
    case JIT_COP_MTC0:
        c_load_eax(e, slot_off(o.rt));  // $0 slot reads 0, like regs[0]
        c_call2(e, (void*)jit_cop0_mtc, o.rd);
        return !e.oom;
    case JIT_COP_MFC2:
        c_call1(e, (void*)jit_cop2_mfc, o.rd);
        c_store_eax(e, slot_off(o.rt));
        return !e.oom;
    case JIT_COP_MTC2:
        c_load_eax(e, slot_off(o.rt));
        c_call2(e, (void*)jit_cop2_mtc, o.rd);
        return !e.oom;
    case JIT_COP_CFC2:
        c_call1(e, (void*)jit_cop2_cfc, o.rd);
        c_store_eax(e, slot_off(o.rt));
        return !e.oom;
    case JIT_COP_CTC2:
        c_load_eax(e, slot_off(o.rt));
        c_call2(e, (void*)jit_cop2_ctc, o.rd);
        return !e.oom;
    case JIT_COP_CUSTOM:
        c_call1(e, (void*)jit_mxu_custom, o.uimm);
        return !e.oom;
    case JIT_COP_MXU1:
        c_call1(e, (void*)jit_mxu1_mov, o.uimm);
        return !e.oom;
    default:
        return false;
    }
}

// ---- reference: same calls the helpers make, on shadow objects ----

void jit_apply_cop_one(const JitAluInsn& o, JitMemState& st) {
    switch (o.op) {
    case JIT_COP_MFC0: st.regs[o.rt] = st.cop0->mfc0((int)o.rd); break;
    case JIT_COP_MTC0: st.cop0->mtc0((int)o.rd, st.regs[o.rt]); break;
    case JIT_COP_MFC2: st.regs[o.rt] = st.mxu->mfc2((int)o.rd); break;
    case JIT_COP_MTC2: st.mxu->mtc2((int)o.rd, st.regs[o.rt]); break;
    case JIT_COP_CFC2: st.regs[o.rt] = st.mxu->cfc2((int)o.rd); break;
    case JIT_COP_CTC2: st.mxu->ctc2((int)o.rd, st.regs[o.rt]); break;
    case JIT_COP_CUSTOM: st.mxu->exec_custom(o.uimm); break;
    case JIT_COP_MXU1: {
        u32 rt = (o.uimm >> 16) & 0x1F;
        u32 rd = (o.uimm >> 11) & 0x1F;
        if ((o.uimm & 0x3F) == 0x2E)
            st.regs[rt] = st.mxu->state.xregs[rd & 15];
        else
            st.mxu->state.xregs[rd & 15] = st.regs[rt];
        break;
    }
    default: break;
    }
}
