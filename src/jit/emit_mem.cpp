#include "emit.h"

#include <cstddef>

static_assert(offsetof(JitState, mem_base) == (size_t)JIT_OFF_MEM_BASE, "mem_base moved");
static_assert(offsetof(JitState, mem_size) == (size_t)JIT_OFF_MEM_SIZE, "mem_size moved");
static_assert(offsetof(JitState, exit_arg) == (size_t)JIT_OFF_EXIT_ARG, "exit_arg moved");
static_assert(offsetof(JitState, code_start) == (size_t)JIT_OFF_CODE_START, "code_start moved");
static_assert(offsetof(JitState, code_end) == (size_t)JIT_OFF_CODE_END, "code_end moved");
static_assert(offsetof(JitState, wc_base) == (size_t)JIT_OFF_WC_BASE, "wc_base moved");

// Phase 3 memory fast path: inline LB/LH/LW/LBU/LHU/SB/SH/SW against the
// host RAM base in JitState. Slow cases exit the TB (JIT_EXIT_SLOW_MEM,
// exit_arg = op index) so the interpreter — which owns palette, GPIO,
// LCD/DMA/IPU logging, log_unmapped, write-protect and write_counts —
// handles them bit-exactly.
//
// Per-op shape (RDX = JitState*, only volatile regs EAX/ECX/RCX clobbered,
// both Win64 and SysV safe):
//   EAX = GPR[rs] + sext(offset)          (wraps mod 2^32 like MIPS)
//   ECX = top-2-bits test -> KSEG0/1 ? EAX &= 0x1FFFFFFF : keep (identity)
//   bounds: ECX = mem_size - size; slow if EAX > ECX (overflow-safe:
//           phys near 4G always exceeds the small bound)
//   stores: slow if code_start <= EAX < code_end (first byte only,
//           mirrors Memory::write_* which checks is_code_section(phys))
//   RCX = mem_base (u64); slow if null (honors the "0 = slow path" contract)
//   fast access via [RCX+RAX] (x86 tolerates unaligned, LE matches MIPS)
//   stores: write_counts[phys>>12]++ via wc_base (skipped if null)
//   loads write GPR[rt] (rt==0 loads never reach here: decoded as NOP)

static void m8(JitEmit& e, u8 v) {
    if (e.len >= e.cap) { e.oom = true; return; }
    e.buf[e.len++] = v;
}

static void m32(JitEmit& e, u32 v) {
    for (u32 i = 0; i < 4; i++) m8(e, (u8)(v >> (i * 8)));
}

static void m_patch32(JitEmit& e, u32 pos) {
    if (pos + 4 > e.cap) { e.oom = true; return; }
    u32 rel = e.len - (pos + 4);
    e.buf[pos] = (u8)rel; e.buf[pos + 1] = (u8)(rel >> 8);
    e.buf[pos + 2] = (u8)(rel >> 16); e.buf[pos + 3] = (u8)(rel >> 24);
}

// 0F cc + rel32 placeholder; returns the placeholder position.
static u32 m_jcc(JitEmit& e, u8 cc) {
    m8(e, 0x0F); m8(e, cc);
    u32 p = e.len;
    m32(e, 0);
    return p;
}

static u32 m_jmp(JitEmit& e) {
    m8(e, 0xE9);
    u32 p = e.len;
    m32(e, 0);
    return p;
}

// mov r32, [RDX+disp32] (r = 0 EAX, 1 ECX): 8B /r mod=10 r/m=010.
static void m_load_state32(JitEmit& e, u32 r, u32 off) {
    m8(e, 0x8B);
    m8(e, (u8)(0x82 | (r << 3)));
    m32(e, off);
}

// EAX = vaddr -> phys per Memory::vaddr_to_phys (KSEG0/1 strip, else identity).
static void m_emit_addr(JitEmit& e, const JitAluInsn& o) {
    if (o.rs == 0)
        { m8(e, 0x31); m8(e, 0xC0); }  // xor eax, eax ($0 reads 0)
    else
        m_load_state32(e, 0, slot_off(o.rs));  // mov eax, [rdx+rs]
    m8(e, 0x05); m32(e, (u32)o.imm);  // add eax, sext(offset)
    m8(e, 0x8B); m8(e, 0xC8);         // mov ecx, eax
    m8(e, 0x81); m8(e, 0xE1); m32(e, 0xC0000000u);  // and ecx, top-2 bits
    m8(e, 0x81); m8(e, 0xF9); m32(e, 0x80000000u);  // cmp ecx, 0x80000000
    u32 jid = m_jcc(e, 0x85);          // jne identity (10xxxxxx = KSEG0/1)
    m8(e, 0x25); m32(e, 0x1FFFFFFFu);  // and eax, 0x1FFFFFFF
    u32 jmp = m_jmp(e);                // jmp bounds
    m_patch32(e, jid);                 // identity:
    m_patch32(e, jmp);                 // bounds:
}

// ECX = mem_size - size; returns the `ja slow` patch position.
static u32 m_emit_bounds(JitEmit& e, u32 size) {
    m_load_state32(e, 1, JIT_OFF_MEM_SIZE);  // mov ecx, [rdx+mem_size]
    m8(e, 0x81); m8(e, 0xE9); m32(e, size);  // sub ecx, size
    m8(e, 0x3B); m8(e, 0xC1);                // cmp eax, ecx
    return m_jcc(e, 0x87);                   // ja slow
}

// RCX = mem_base; returns the `jz slow` patch position.
static u32 m_emit_base(JitEmit& e) {
    m8(e, 0x48); m8(e, 0x8B); m8(e, 0x8A); m32(e, JIT_OFF_MEM_BASE);
    m8(e, 0x48); m8(e, 0x85); m8(e, 0xC9);  // test rcx, rcx
    return m_jcc(e, 0x84);                   // jz slow
}

static void m_emit_slow(JitEmit& e, u32 idx) {
    m8(e, 0xC7); m8(e, 0x82); m32(e, JIT_OFF_EXIT_ARG); m32(e, idx);
    // Partial tick accounting: idx ops completed before the slow one.
    // (The DONE path adds the full count; see jit_compile_tb.)
    if (idx > 0 && idx < 128) {
        m8(e, 0x83); m8(e, 0x82); m32(e, JIT_OFF_TICK_DELTA); m8(e, (u8)idx);
    }
    m8(e, 0xB8); m32(e, (u32)JIT_EXIT_SLOW_MEM);  // mov eax, SLOW_MEM
    m8(e, 0xC3);                                   // ret
}

bool emit_mem_op(JitEmit& e, const JitAluInsn& o, u32 op_idx) {
    u32 size = 0;
    bool is_store = false, is_load = false;
    u32 load_kind = 0;  // 0 LB 1 LH 2 LW 3 LBU 4 LHU
    u32 store_kind = 0;  // 0 SB 1 SH 2 SW
    switch (o.op) {
    case JIT_ALU_LB:  size = 1; is_load = true; load_kind = 0; break;
    case JIT_ALU_LH:  size = 2; is_load = true; load_kind = 1; break;
    case JIT_ALU_LW:  size = 4; is_load = true; load_kind = 2; break;
    case JIT_ALU_LBU: size = 1; is_load = true; load_kind = 3; break;
    case JIT_ALU_LHU: size = 2; is_load = true; load_kind = 4; break;
    case JIT_ALU_SB:  size = 1; is_store = true; store_kind = 0; break;
    case JIT_ALU_SH:  size = 2; is_store = true; store_kind = 1; break;
    case JIT_ALU_SW:  size = 4; is_store = true; store_kind = 2; break;
    default: return false;
    }

    u32 slow_patches[6];
    u32 nslow = 0;

    m_emit_addr(e, o);
    slow_patches[nslow++] = m_emit_bounds(e, size);

    if (is_store) {
        // Code-section reject (first byte only, mirrors Memory::write_*).
        m_load_state32(e, 1, JIT_OFF_CODE_START);  // mov ecx, [rdx+code_start]
        m8(e, 0x3B); m8(e, 0xC1);                  // cmp eax, ecx
        u32 jok = m_jcc(e, 0x82);                  // jb ok (phys < start)
        m_load_state32(e, 1, JIT_OFF_CODE_END);
        m8(e, 0x3B); m8(e, 0xC1);                  // cmp eax, ecx
        slow_patches[nslow++] = m_jcc(e, 0x82);     // jb slow (inside range)
        m_patch32(e, jok);                         // ok:
    }

    slow_patches[nslow++] = m_emit_base(e);

    if (is_load) {
        // Fast load via [RCX+RAX]: ModRM mod=00 r/m=100 (SIB),
        // SIB 00 000 001 = base RCX + index RAX, no displacement.
        if (load_kind == 2) m8(e, 0x8B);          // mov eax, [rcx+rax]
        else if (load_kind == 0) { m8(e, 0x0F); m8(e, 0xBE); }  // movsx byte
        else if (load_kind == 1) { m8(e, 0x0F); m8(e, 0xBF); }  // movsx word
        else if (load_kind == 3) { m8(e, 0x0F); m8(e, 0xB6); }  // movzx byte
        else { m8(e, 0x0F); m8(e, 0xB7); }                      // movzx word
        m8(e, 0x04); m8(e, 0x01);  // ModRM mod=00 r/m=SIB + SIB(base RCX, idx RAX)
        m8(e, 0x89); m8(e, 0x82); m32(e, slot_off(o.rt));  // mov [rdx+rt], eax
    } else {
        // Value into R8D (NOT ECX: a 32-bit write to ECX would zero the
        // high half of RCX and destroy the mem_base pointer — that exact
        // bug segfaulted the first store discharge test).
        // mov r8d, [rdx+rt]: REX.R + 8B mod=10 r/m=010.
        if (o.rt == 0) { m8(e, 0x45); m8(e, 0x31); m8(e, 0xC0); }  // xor r8d, r8d
        else { m8(e, 0x44); m8(e, 0x8B); m8(e, 0x82); m32(e, slot_off(o.rt)); }
        // Fast store via [RCX+RAX] from R8 (REX.R extends the reg field;
        // legacy 0x66 prefix precedes REX for the 16-bit form).
        if (store_kind == 0) {        // mov [rcx+rax], r8b
            m8(e, 0x44); m8(e, 0x88); m8(e, 0x04); m8(e, 0x01);
        } else if (store_kind == 1) {  // mov [rcx+rax], r8w
            m8(e, 0x66); m8(e, 0x44); m8(e, 0x89); m8(e, 0x04); m8(e, 0x01);
        } else {                       // mov [rcx+rax], r8d
            m8(e, 0x44); m8(e, 0x89); m8(e, 0x04); m8(e, 0x01);
        }
        // write_counts[phys>>12]++ via wc_base (skipped when null).
        m8(e, 0xC1); m8(e, 0xE8); m8(e, 0x0C);  // shr eax, 12
        m8(e, 0x48); m8(e, 0x8B); m8(e, 0x8A); m32(e, JIT_OFF_WC_BASE);
        m8(e, 0x48); m8(e, 0x85); m8(e, 0xC9);  // test rcx, rcx
        u32 jwc = m_jcc(e, 0x84);               // jz done
        m8(e, 0xFF); m8(e, 0x04); m8(e, 0x81);  // inc dword [rcx+rax*4]
        m_patch32(e, jwc);
    }

    u32 jdone = m_jmp(e);
    for (u32 i = 0; i < nslow; i++) m_patch32(e, slow_patches[i]);  // slow:
    m_emit_slow(e, op_idx);
    m_patch32(e, jdone);  // done:
    return !e.oom;
}

// ---- reference (plain C++, same semantics as the bytes above) ----

static u32 mem_phys(u32 vaddr) {
    // Mirrors Memory::vaddr_to_phys: KSEG0/1 strip, else identity.
    if ((vaddr & 0xC0000000u) == 0x80000000u)
        return vaddr & 0x1FFFFFFFu;
    return vaddr;
}

u32 jit_run_mem_reference(const JitTbPlan& plan, JitMemState& st, u32& fail_idx) {
    fail_idx = 0;
    for (u32 i = 0; i < plan.count; i++) {
        const JitAluInsn& o = plan.ops[i];
        // Tick accounting mirrors the emitter: DONE adds plan.count, slow
        // exits add the completed-op count (see m_emit_slow). Bump per op
        // here; the slow return below stops before the failing op's tick.
        if (st.tick_delta)
            (*st.tick_delta)++;
        switch (o.op) {
        case JIT_ALU_LB: case JIT_ALU_LH: case JIT_ALU_LW:
        case JIT_ALU_LBU: case JIT_ALU_LHU:
        case JIT_ALU_SB: case JIT_ALU_SH: case JIT_ALU_SW: {
            bool is_store = (o.op == JIT_ALU_SB || o.op == JIT_ALU_SH ||
                             o.op == JIT_ALU_SW);
            u32 size = (o.op == JIT_ALU_LB || o.op == JIT_ALU_LBU ||
                        o.op == JIT_ALU_SB) ? 1
                     : (o.op == JIT_ALU_LH || o.op == JIT_ALU_LHU ||
                        o.op == JIT_ALU_SH) ? 2 : 4;
            u32 vaddr = st.regs[o.rs] + (u32)o.imm;
            u32 phys = mem_phys(vaddr);
            bool slow = false;
            if (!st.ram || st.ram_size < size)
                slow = true;
            else if (phys > st.ram_size - size)
                slow = true;
            else if (is_store && phys >= st.code_start && phys < st.code_end)
                slow = true;
            if (slow) {
                // The failing op's tick was pre-bumped above; take it back
                // (the emitter counts only completed ops: exit_arg == idx).
                if (st.tick_delta)
                    (*st.tick_delta)--;
                fail_idx = i;
                return (u32)JIT_EXIT_SLOW_MEM;
            }
            if (!is_store) {
                u32 v = 0;
                if (size == 1) v = st.ram[phys];
                else if (size == 2) v = (u32)st.ram[phys] | ((u32)st.ram[phys + 1] << 8);
                else v = (u32)st.ram[phys] | ((u32)st.ram[phys + 1] << 8) |
                         ((u32)st.ram[phys + 2] << 16) | ((u32)st.ram[phys + 3] << 24);
                if (o.op == JIT_ALU_LB) v = (u32)(s32)(s8)v;
                if (o.op == JIT_ALU_LH) v = (u32)(s32)(s16)v;
                st.regs[o.rt] = v;
            } else {
                u32 v = st.regs[o.rt];
                if (size == 1) st.ram[phys] = (u8)v;
                else if (size == 2) {
                    st.ram[phys] = (u8)v;
                    st.ram[phys + 1] = (u8)(v >> 8);
                } else {
                    st.ram[phys] = (u8)v;
                    st.ram[phys + 1] = (u8)(v >> 8);
                    st.ram[phys + 2] = (u8)(v >> 16);
                    st.ram[phys + 3] = (u8)(v >> 24);
                }
                if (st.wc && (phys >> 12) < st.wc_pages)
                    st.wc[phys >> 12]++;
            }
            break;
        }
        case JIT_COP_MFC0: case JIT_COP_MTC0:
        case JIT_COP_MFC2: case JIT_COP_MTC2:
        case JIT_COP_CFC2: case JIT_COP_CTC2:
        case JIT_COP_CUSTOM: case JIT_COP_MXU1:
            // Phase 4: same calls the emitted helpers make (emit_cop.cpp).
            jit_apply_cop_one(o, st);
            break;
        default:
            // ALU/branch ops share semantics with the ALU reference.
            jit_apply_alu(o, st.regs, st.hi, st.lo);
            break;
        }
    }
    return (u32)JIT_EXIT_DONE;
}
