#include "emit.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

static_assert(offsetof(JitState, gpr) == 0, "gpr base moved");
static_assert(sizeof(((JitState*)0)->gpr) == 128, "gpr size changed");
static_assert(offsetof(JitState, exit_arg) == 132, "exit_arg moved");
static_assert(offsetof(JitState, hi) == 144, "hi moved");
static_assert(offsetof(JitState, lo) == 148, "lo moved");
static_assert(offsetof(JitState, mem_base) == 152, "mem_base moved");
static_assert(offsetof(JitState, mem_size) == 160, "mem_size moved");
static_assert(offsetof(JitState, code_start) == 164, "code_start moved");
static_assert(offsetof(JitState, code_end) == 168, "code_end moved");
static_assert(offsetof(JitState, wc_base) == 176, "wc_base moved");
static_assert(offsetof(JitState, insn_delta) == JIT_OFF_INSN_DELTA, "insn_delta moved");
static_assert(JIT_PROLOG_CHAIN_OFF == 5, "chain entry offset drift");

static void emit_u8(JitEmit& e, u8 v) {
    if (e.len >= e.cap) { e.oom = true; return; }
    e.buf[e.len++] = v;
}

static void emit_u32(JitEmit& e, u32 v) {
    for (u32 i = 0; i < 4; i++) emit_u8(e, (u8)(v >> (i * 8)));
}

// Patch a rel32 placeholder at `pos` to jump to the current end.
static void patch_rel32(JitEmit& e, u32 pos) {
    if (pos + 4 > e.cap) { e.oom = true; return; }
    u32 rel = e.len - (pos + 4);
    e.buf[pos] = (u8)rel; e.buf[pos+1] = (u8)(rel >> 8);
    e.buf[pos+2] = (u8)(rel >> 16); e.buf[pos+3] = (u8)(rel >> 24);
}

// Patch a rel8 placeholder at `pos` to jump to the current end.
static void patch_rel8(JitEmit& e, u32 pos) {
    if (pos >= e.cap) { e.oom = true; return; }
    u32 rel = e.len - (pos + 1);
    if (rel > 127) { e.oom = true; return; }  // caller keeps sequences short
    e.buf[pos] = (u8)rel;
}

// IMUL/IDIV/DIV clobber EDX (HI product). Push ESI (state base) first; after
// the op, pop to ECX, store LO/HI, restore ESI.
static void emit_push_state(JitEmit& e) { emit_u8(e, 0x56); }  // push esi
static void emit_pop_store_hilo(JitEmit& e) {
    emit_u8(e, 0x59);  // pop ecx (saved state base)
    emit_u8(e, 0x89); emit_u8(e, 0x81); emit_u32(e, JIT_OFF_LO_VAL);  // [ecx+LO]=eax
    emit_u8(e, 0x89); emit_u8(e, 0x91); emit_u32(e, JIT_OFF_HI_VAL);  // [ecx+HI]=edx
    emit_u8(e, 0x89); emit_u8(e, 0xCE);  // mov esi, ecx
}

// mov r32, [ESI+disp32]: host 0=EAX 1=ECX.
// Encoding: 8B /r with mod=10, r/m=110 (ESI+disp32, no SIB needed).
// (mod=00+SIB is WRONG here: mod=00 means no displacement, the disp32
// bytes would decode as trailing garbage. That bug hung --jit-tests.)
static void emit_load_slot(JitEmit& e, u32 host, u32 off) {
    emit_u8(e, 0x8B);
    emit_u8(e, (u8)(0x86 | (host << 3)));
    emit_u32(e, off);
}

// mov [ESI+disp32], r32 (host 0=EAX 1=ECX): 89 /r mod=10 r/m=110.
static void emit_store_reg(JitEmit& e, u32 off, u32 host) {
    emit_u8(e, 0x89);
    emit_u8(e, (u8)(0x86 | (host << 3)));
    emit_u32(e, off);
}

// mov [RDX+disp32], EAX
static void emit_store_slot(JitEmit& e, u32 off) {
    emit_store_reg(e, off, 0);
}

// mov r32, imm32 (host 0=EAX 1=ECX)
static void emit_mov_imm_local(JitEmit& e, u32 host, u32 imm) {
    emit_u8(e, (u8)(0xB8 | (host & 7)));
    emit_u32(e, imm);
}

// shift EAX, imm: 4=SHL 5=SHR 7=SAR
static void emit_shift_imm(JitEmit& e, u32 sub, u32 n) {
    emit_u8(e, 0xC1);
    emit_u8(e, (u8)(0xC0 | (sub << 3)));
    emit_u8(e, (u8)n);
}

// ALU r/m32, imm32: 81 /sub -- 0 ADD 5 SUB 4 AND 1 OR 6 XOR 7 CMP
static void emit_alu_imm(JitEmit& e, u32 sub, u32 imm) {
    emit_u8(e, 0x81);
    emit_u8(e, (u8)(0xC0 | (sub << 3)));
    emit_u32(e, imm);
}

// ALU EAX, ECX: 03 ADD / 2B SUB / 23 AND / 0B OR / 33 XOR / 3B CMP
static void emit_alu_eax_ecx(JitEmit& e, u8 op) {
    emit_u8(e, op);
    emit_u8(e, 0xC1);
}
// Forward declaration for emit_prolog

static void emit_prolog(JitEmit& e, u32 entry_pc, u32 count) {
    // cdecl: save caller ESI, then load JitState* (now at [esp+8] after push esi).
    emit_u8(e, 0x56);  // push esi
    emit_u8(e, 0x8B); emit_u8(e, 0x74); emit_u8(e, 0x24); emit_u8(e, 0x08);  // mov esi,[esp+8]
    emit_mov_rdx_disp32(e, JIT_OFF_PC, entry_pc);
    if (count > 0 && count <= 127) {
        emit_u8(e, 0x83);
        emit_u8(e, 0x86);
        emit_u32(e, JIT_OFF_INSN_DELTA);
        emit_u8(e, (u8)count);
    }
}

static void emit_epilog(JitEmit& e, u32 exit_code) {
    emit_mov_imm_local(e, 0, exit_code);
    emit_u8(e, 0x5E);  // pop esi (restore caller)
    emit_u8(e, 0xC3);  // ret
}

static void ext_fields(u32 insn, u32& pos, u32& size) {
    // Mirrors cpu.cpp exec_special3: pos = sa, size = rd + 1.
    // (rd encodes size-1 for EXT, NOT msb — size is NOT msb-lsb+1.)
    pos = (insn >> 6) & 0x1F;
    size = ((insn >> 11) & 0x1F) + 1;
}

static void ins_fields(u32 insn, u32& pos, u32& size) {
    // Mirrors cpu.cpp exec_special3: pos = sa, size = rd + 1.
    pos = (insn >> 6) & 0x1F;
    size = ((insn >> 11) & 0x1F) + 1;
}

static u32 mask32(u32 size) {
    if (size >= 32) return 0xFFFFFFFFu;
    if (size == 0) return 0u;
    return (1u << size) - 1u;
}

// Non-inline shared helpers declared in emit.h.
void emit_mov_imm(JitEmit& e, u32 host_reg, u32 imm) {
    emit_mov_imm_local(e, host_reg, imm);
}

// Emit mov [RDX+disp32], imm32: 81 02 <off32> <imm32>.
void emit_mov_rdx_disp32(JitEmit& e, u32 disp32, u32 imm) {
    // mov dword ptr [rdx+disp32], imm32
    emit_u8(e, 0xC7);
    emit_u8(e, 0x86);
    emit_u32(e, disp32);
    emit_u32(e, imm);
}

static bool emit_one(JitEmit& e, const JitAluInsn& o, u32 idx) {
    switch (o.op) {
    case JIT_ALU_NOP:
        return true;
    // Phase 3 fast-path loads/stores (emit_mem.cpp). LWL/LWR/SWL/SWR,
    // LL/SC, CACHE, LWCx/SWCx never reach here (frontend STOP_MEM).
    case JIT_ALU_LB: case JIT_ALU_LH: case JIT_ALU_LW:
    case JIT_ALU_LBU: case JIT_ALU_LHU:
    case JIT_ALU_SB: case JIT_ALU_SH: case JIT_ALU_SW:
        return emit_mem_op(e, o, idx);
    // Phase 4 COP0/COP2 (emit_cop.cpp): calls into cop0.cpp/mxu.cpp.
    // ERET never reaches here (frontend STOP_ERET).
    case JIT_COP_MFC0: case JIT_COP_MTC0:
    case JIT_COP_MFC2: case JIT_COP_MTC2:
    case JIT_COP_CFC2: case JIT_COP_CTC2:
    case JIT_COP_CUSTOM: case JIT_COP_MXU1:
        return emit_cop_op(e, o);
    case JIT_ALU_SLL:
        emit_load_slot(e, 0, slot_off(o.rt));
        emit_shift_imm(e, 4, o.sa);
        emit_store_slot(e, slot_off(o.rd));
        return true;
    case JIT_ALU_SRL:
        emit_load_slot(e, 0, slot_off(o.rt));
        emit_shift_imm(e, 5, o.sa);
        emit_store_slot(e, slot_off(o.rd));
        return true;
    case JIT_ALU_SRA:
        emit_load_slot(e, 0, slot_off(o.rt));
        emit_shift_imm(e, 7, o.sa);
        emit_store_slot(e, slot_off(o.rd));
        return true;
    case JIT_ALU_ADDU:
        emit_load_slot(e, 0, slot_off(o.rt));
        emit_load_slot(e, 1, slot_off(o.rs));
        emit_alu_eax_ecx(e, 0x03);
        emit_store_slot(e, slot_off(o.rd));
        return true;
    case JIT_ALU_SUBU:
        emit_load_slot(e, 0, slot_off(o.rs));
        emit_load_slot(e, 1, slot_off(o.rt));
        emit_alu_eax_ecx(e, 0x2B);  // sub eax, ecx: rd = rs - rt
        emit_store_slot(e, slot_off(o.rd));
        return true;
    case JIT_ALU_AND:
        emit_load_slot(e, 0, slot_off(o.rt));
        emit_load_slot(e, 1, slot_off(o.rs));
        emit_alu_eax_ecx(e, 0x23);
        emit_store_slot(e, slot_off(o.rd));
        return true;
    case JIT_ALU_OR:
        emit_load_slot(e, 0, slot_off(o.rt));
        emit_load_slot(e, 1, slot_off(o.rs));
        emit_alu_eax_ecx(e, 0x0B);
        emit_store_slot(e, slot_off(o.rd));
        return true;
    case JIT_ALU_XOR:
        emit_load_slot(e, 0, slot_off(o.rt));
        emit_load_slot(e, 1, slot_off(o.rs));
        emit_alu_eax_ecx(e, 0x33);
        emit_store_slot(e, slot_off(o.rd));
        return true;
    case JIT_ALU_NOR:
        emit_load_slot(e, 0, slot_off(o.rt));
        emit_load_slot(e, 1, slot_off(o.rs));
        emit_alu_eax_ecx(e, 0x0B);
        emit_u8(e, 0xF7); emit_u8(e, 0xD0);
        emit_store_slot(e, slot_off(o.rd));
        return true;
    case JIT_ALU_SLT:
        emit_load_slot(e, 0, slot_off(o.rs));
        emit_load_slot(e, 1, slot_off(o.rt));
        emit_alu_eax_ecx(e, 0x3B);
        emit_u8(e, 0x0F); emit_u8(e, 0x9C); emit_u8(e, 0xC0);
        emit_u8(e, 0x0F); emit_u8(e, 0xB6); emit_u8(e, 0xC0);
        emit_store_slot(e, slot_off(o.rd));
        return true;
    case JIT_ALU_SLTU:
        emit_load_slot(e, 0, slot_off(o.rs));
        emit_load_slot(e, 1, slot_off(o.rt));
        emit_alu_eax_ecx(e, 0x3B);
        emit_u8(e, 0x0F); emit_u8(e, 0x92); emit_u8(e, 0xC0);
        emit_u8(e, 0x0F); emit_u8(e, 0xB6); emit_u8(e, 0xC0);
        emit_store_slot(e, slot_off(o.rd));
        return true;
    case JIT_ALU_MOVZ: {
        // if (rt == 0) rd = rs. Skip distance is patched (load+store size
        // depends on the slot encoding; never hardcode it).
        emit_load_slot(e, 1, slot_off(o.rt));
        emit_u8(e, 0x85); emit_u8(e, 0xC9);  // test ecx, ecx
        emit_u8(e, 0x75);                    // jne skip
        u32 rel = e.len; emit_u8(e, 0);
        emit_load_slot(e, 0, slot_off(o.rs));
        emit_store_slot(e, slot_off(o.rd));
        patch_rel8(e, rel);
        return true;
    }
    case JIT_ALU_MOVN: {
        // if (rt != 0) rd = rs.
        emit_load_slot(e, 1, slot_off(o.rt));
        emit_u8(e, 0x85); emit_u8(e, 0xC9);  // test ecx, ecx
        emit_u8(e, 0x74);                    // je skip
        u32 rel = e.len; emit_u8(e, 0);
        emit_load_slot(e, 0, slot_off(o.rs));
        emit_store_slot(e, slot_off(o.rd));
        patch_rel8(e, rel);
        return true;
    }
    case JIT_ALU_MFHI:
        emit_load_slot(e, 0, JIT_OFF_HI_VAL);
        emit_store_slot(e, slot_off(o.rd));
        return true;
    case JIT_ALU_MTHI:
        emit_load_slot(e, 0, slot_off(o.rs));
        emit_store_reg(e, JIT_OFF_HI_VAL, 0);
        return true;
    case JIT_ALU_MFLO:
        emit_load_slot(e, 0, JIT_OFF_LO_VAL);
        emit_store_slot(e, slot_off(o.rd));
        return true;
    case JIT_ALU_MTLO:
        emit_load_slot(e, 0, slot_off(o.rs));
        emit_store_reg(e, JIT_OFF_LO_VAL, 0);
        return true;
    case JIT_ALU_MULT: {
        emit_load_slot(e, 0, slot_off(o.rt));
        emit_load_slot(e, 1, slot_off(o.rs));
        emit_push_state(e);
        emit_u8(e, 0xF7); emit_u8(e, 0xE9);  // imul ecx (edx:eax = eax*ecx)
        emit_pop_store_hilo(e);
        return true;
    }
    case JIT_ALU_MULTU: {
        emit_load_slot(e, 0, slot_off(o.rt));
        emit_load_slot(e, 1, slot_off(o.rs));
        emit_push_state(e);
        emit_u8(e, 0xF7); emit_u8(e, 0xE1);  // mul ecx
        emit_pop_store_hilo(e);
        return true;
    }

    case JIT_ALU_DIV: {
        // Skip when rt == 0 (matches interpreter). Also skip INT_MIN / -1:
        // x86 IDIV faults (#DE) there while the C++ interpreter has UB —
        // skipping keeps the JIT crash-free and matches the div-by-zero policy.
        emit_load_slot(e, 1, slot_off(o.rt));
        emit_u8(e, 0x85); emit_u8(e, 0xC9);  // test ecx, ecx
        emit_u8(e, 0x0F); emit_u8(e, 0x84);  // jz skip
        u32 dp = e.len; emit_u32(e, 0);
        emit_load_slot(e, 0, slot_off(o.rs));
        emit_u8(e, 0x3D); emit_u32(e, 0x80000000u);  // cmp eax, INT_MIN
        emit_u8(e, 0x0F); emit_u8(e, 0x85);          // jne do_div
        u32 dp2 = e.len; emit_u32(e, 0);
        emit_u8(e, 0x81); emit_u8(e, 0xF9); emit_u32(e, 0xFFFFFFFFu);  // cmp ecx, -1
        emit_u8(e, 0x0F); emit_u8(e, 0x84);          // je skip
        u32 dp3 = e.len; emit_u32(e, 0);
        patch_rel32(e, dp2);  // do_div:
        emit_push_state(e);
        emit_u8(e, 0x99);                             // cdq
        emit_u8(e, 0xF7); emit_u8(e, 0xF9);            // idiv ecx
        emit_pop_store_hilo(e);
        patch_rel32(e, dp);   // skip:
        patch_rel32(e, dp3);
        return true;
    }
    case JIT_ALU_DIVU: {
        emit_load_slot(e, 1, slot_off(o.rt));
        emit_u8(e, 0x85); emit_u8(e, 0xC9);
        emit_u8(e, 0x0F); emit_u8(e, 0x84);
        u32 dq = e.len; emit_u32(e, 0);
        emit_load_slot(e, 0, slot_off(o.rs));
        emit_push_state(e);
        emit_u8(e, 0x31); emit_u8(e, 0xD2);  // xor edx, edx
        emit_u8(e, 0xF7); emit_u8(e, 0xF1);  // div ecx
        emit_pop_store_hilo(e);
        patch_rel32(e, dq);
        return true;
    }
    case JIT_ALU_MUL: {
        emit_load_slot(e, 0, slot_off(o.rt));
        emit_load_slot(e, 1, slot_off(o.rs));
        emit_u8(e, 0x0F); emit_u8(e, 0xAF); emit_u8(e, 0xC1);
        emit_store_slot(e, slot_off(o.rd));
        return true;
    }
    case JIT_ALU_ADDI:
        emit_load_slot(e, 0, slot_off(o.rs));
        emit_alu_imm(e, 0, (u32)o.imm);
        emit_store_slot(e, slot_off(o.rt));
        return true;
    case JIT_ALU_ADDIU:
        emit_load_slot(e, 0, slot_off(o.rs));
        emit_alu_imm(e, 0, (u32)o.imm);
        emit_store_slot(e, slot_off(o.rt));
        return true;
    case JIT_ALU_SLTI:
        emit_load_slot(e, 0, slot_off(o.rs));
        emit_alu_imm(e, 7, (u32)o.imm);
        emit_u8(e, 0x0F); emit_u8(e, 0x9C); emit_u8(e, 0xC0);
        emit_u8(e, 0x0F); emit_u8(e, 0xB6); emit_u8(e, 0xC0);
        emit_store_slot(e, slot_off(o.rt));
        return true;
    case JIT_ALU_SLTIU:
        // cpu.cpp compares against the SIGN-extended imm as u32:
        // regs[rt] = regs[rs] < (u32)imm. (NOT the zero-extended uimm.)
        emit_load_slot(e, 0, slot_off(o.rs));
        emit_alu_imm(e, 7, (u32)o.imm);
        emit_u8(e, 0x0F); emit_u8(e, 0x92); emit_u8(e, 0xC0);
        emit_u8(e, 0x0F); emit_u8(e, 0xB6); emit_u8(e, 0xC0);
        emit_store_slot(e, slot_off(o.rt));
        return true;
    case JIT_ALU_ANDI:
        emit_load_slot(e, 0, slot_off(o.rs));
        emit_alu_imm(e, 4, o.uimm);
        emit_store_slot(e, slot_off(o.rt));
        return true;
    case JIT_ALU_ORI:
        emit_load_slot(e, 0, slot_off(o.rs));
        emit_alu_imm(e, 1, o.uimm);
        emit_store_slot(e, slot_off(o.rt));
        return true;
    case JIT_ALU_XORI:
        emit_load_slot(e, 0, slot_off(o.rs));
        emit_alu_imm(e, 6, o.uimm);
        emit_store_slot(e, slot_off(o.rt));
        return true;
    case JIT_ALU_LUI:
        emit_mov_imm_local(e, 0, o.uimm << 16);
        emit_store_slot(e, slot_off(o.rt));
        return true;
    case JIT_ALU_EXT: {
        u32 pos, size;
        ext_fields((u32)o.imm, pos, size);
        emit_load_slot(e, 0, slot_off(o.rs));
        if (pos) emit_shift_imm(e, 5, pos);
        u32 m = mask32(size);
        if (m != 0xFFFFFFFFu) emit_alu_imm(e, 4, m);
        emit_store_slot(e, slot_off(o.rt));
        return true;
    }
    case JIT_ALU_INS: {
        u32 pos, size;
        ins_fields((u32)o.imm, pos, size);
        u32 m = mask32(size);
        emit_load_slot(e, 1, slot_off(o.rs));
        if (m != 0xFFFFFFFFu) {
            emit_u8(e, 0x81); emit_u8(e, 0xE1); emit_u32(e, m);
        }
        if (pos) {
            emit_u8(e, 0xC1); emit_u8(e, 0xE1); emit_u8(e, (u8)pos);
        }
        emit_load_slot(e, 0, slot_off(o.rt));
        if (size == 32) {
            emit_mov_imm_local(e, 0, 0);
            emit_u8(e, 0x0B); emit_u8(e, 0xC1);
        } else {
            u32 field = m << pos;
            emit_u8(e, 0x81); emit_u8(e, 0xE0); emit_u32(e, ~field);
            emit_u8(e, 0x09); emit_u8(e, 0xC8);
            emit_mov_imm_local(e, 1, 0);
            emit_u8(e, 0x0B); emit_u8(e, 0xC1);
        }
        emit_store_slot(e, slot_off(o.rt));
        return true;
    }
    case JIT_ALU_CLZ:
    case JIT_ALU_CLO:
        return false;
    }
    return false;
}

bool jit_emit_op(JitEmit& e, const JitAluInsn& op, u32 op_idx) {
    if (!emit_one(e, op, op_idx))
        return false;
    return !e.oom;
}

static u32 ref_clz(u32 v) {
    if (v == 0) return 32;
    u32 c = 0;
    while ((v & 0x80000000u) == 0) { v <<= 1; c++; }
    return c;
}

void jit_apply_alu(const JitAluInsn& o, u32 regs[32], u32& hi, u32& lo) {
    u32 h = hi, l = lo;
    switch (o.op) {
        case JIT_ALU_NOP: break;
        case JIT_ALU_SLL: regs[o.rd] = regs[o.rt] << (o.sa & 0x1F); break;
        case JIT_ALU_SRL: regs[o.rd] = regs[o.rt] >> (o.sa & 0x1F); break;
        case JIT_ALU_SRA:
            regs[o.rd] = (u32)((s32)regs[o.rt] >> (o.sa & 0x1F));
            break;
        case JIT_ALU_ADDU: regs[o.rd] = regs[o.rs] + regs[o.rt]; break;
        case JIT_ALU_SUBU: regs[o.rd] = regs[o.rs] - regs[o.rt]; break;
        case JIT_ALU_AND: regs[o.rd] = regs[o.rs] & regs[o.rt]; break;
        case JIT_ALU_OR: regs[o.rd] = regs[o.rs] | regs[o.rt]; break;
        case JIT_ALU_XOR: regs[o.rd] = regs[o.rs] ^ regs[o.rt]; break;
        case JIT_ALU_NOR: regs[o.rd] = ~(regs[o.rs] | regs[o.rt]); break;
        case JIT_ALU_SLT:
            regs[o.rd] = (s32)regs[o.rs] < (s32)regs[o.rt] ? 1 : 0;
            break;
        case JIT_ALU_SLTU: regs[o.rd] = regs[o.rs] < regs[o.rt] ? 1 : 0; break;
        case JIT_ALU_MOVZ: if (regs[o.rt] == 0) regs[o.rd] = regs[o.rs]; break;
        case JIT_ALU_MOVN: if (regs[o.rt] != 0) regs[o.rd] = regs[o.rs]; break;
        case JIT_ALU_MFHI: regs[o.rd] = h; break;
        case JIT_ALU_MTHI: h = regs[o.rs]; break;
        case JIT_ALU_MFLO: regs[o.rd] = l; break;
        case JIT_ALU_MTLO: l = regs[o.rs]; break;
        case JIT_ALU_MULT: {
            s64 r = (s64)(s32)regs[o.rs] * (s64)(s32)regs[o.rt];
            l = (u32)r; h = (u32)((u64)r >> 32);
            break;
        }
        case JIT_ALU_MULTU: {
            u64 r = (u64)regs[o.rs] * (u64)regs[o.rt];
            l = (u32)r; h = (u32)(r >> 32);
            break;
        }
        case JIT_ALU_DIV:
            // Skip on rt==0 and on INT_MIN/-1 (emitter can't IDIV it —
            // x86 #DE; interpreter has UB there). No hi/lo write either way.
            if (regs[o.rt] &&
                !(regs[o.rs] == 0x80000000u && regs[o.rt] == 0xFFFFFFFFu)) {
                l = (u32)((s32)regs[o.rs] / (s32)regs[o.rt]);
                h = (u32)((s32)regs[o.rs] % (s32)regs[o.rt]);
            }
            break;
        case JIT_ALU_DIVU:
            if (regs[o.rt]) {
                l = regs[o.rs] / regs[o.rt];
                h = regs[o.rs] % regs[o.rt];
            }
            break;
        case JIT_ALU_MUL: regs[o.rd] = regs[o.rs] * regs[o.rt]; break;
        case JIT_ALU_ADDI: regs[o.rt] = regs[o.rs] + (u32)o.imm; break;
        case JIT_ALU_ADDIU: regs[o.rt] = regs[o.rs] + (u32)o.imm; break;
        case JIT_ALU_SLTI:
            regs[o.rt] = (s32)regs[o.rs] < o.imm ? 1 : 0;
            break;
        case JIT_ALU_SLTIU: regs[o.rt] = regs[o.rs] < (u32)o.imm ? 1 : 0; break;
        case JIT_ALU_ANDI: regs[o.rt] = regs[o.rs] & o.uimm; break;
        case JIT_ALU_ORI: regs[o.rt] = regs[o.rs] | o.uimm; break;
        case JIT_ALU_XORI: regs[o.rt] = regs[o.rs] ^ o.uimm; break;
        case JIT_ALU_LUI: regs[o.rt] = o.uimm << 16; break;
        case JIT_ALU_EXT: {
            u32 pos, size;
            ext_fields((u32)o.imm, pos, size);
            u32 m = mask32(size);
            regs[o.rt] = (regs[o.rs] >> pos) & m;
            break;
        }
        case JIT_ALU_INS: {
            u32 pos, size;
            ins_fields((u32)o.imm, pos, size);
            u32 m = mask32(size);
            u32 field = (size >= 32) ? 0xFFFFFFFFu : (m << pos);
            regs[o.rt] = (regs[o.rt] & ~field) | ((regs[o.rs] & m) << pos);
            break;
        }
        case JIT_ALU_CLZ: regs[o.rd] = ref_clz(regs[o.rs]); break;
        case JIT_ALU_CLO: regs[o.rd] = ref_clz(~regs[o.rs]); break;
        default: break;  // branches, mem ops (mem ref handles those), traps
        }
    hi = h;
    lo = l;
}

void jit_run_reference(const JitTbPlan& plan, u32 regs[32], u32* hi, u32* lo) {
    u32 h = hi ? *hi : 0, l = lo ? *lo : 0;
    for (u32 i = 0; i < plan.count; i++)
        jit_apply_alu(plan.ops[i], regs, h, l);
    if (hi) *hi = h;
    if (lo) *lo = l;
}

u32 jit_compile_tb(const JitTbPlan& plan, u8* buf, u32 cap, u32 exit_code,
                   u32 entry_pc) {
    JitEmit e{buf, cap, 0, false};
    emit_prolog(e, entry_pc, plan.count);
    for (u32 i = 0; i < plan.count; i++) {
        if (!emit_one(e, plan.ops[i], i)) return 0;
        if (e.oom) return 0;
    }
    // Phase 4 tick accounting: one cop0.tick() per executed op. The DONE
    // path ran all plan.count ops; slow-mem exits add their own partial
    // count (see emit_mem.cpp). Phase 5 flushes tick_delta into COP0.
    // ADD r/m32,imm8 (83 /0 ib): plan.count <= 64 always fits.
    if (plan.count > 0) {
        emit_u8(e, 0x83); emit_u8(e, 0x86); emit_u32(e, JIT_OFF_TICK_DELTA);
        emit_u8(e, (u8)plan.count);
    }
    if (exit_code == (u32)JIT_EXIT_DONE)
        emit_mov_rdx_disp32(e, JIT_OFF_NEXT_PC, entry_pc + plan.count * 4);
    emit_epilog(e, exit_code);
    if (e.oom) return 0;
    return e.len;
}

// Phase 6 shared primitives for branch-TB assembly (emit_branch.cpp).
void jit_emit_prolog(JitEmit& e, u32 entry_pc, u32 count) {
    emit_prolog(e, entry_pc, count);
}
void jit_emit_epilog(JitEmit& e, u32 exit_code) { emit_epilog(e, exit_code); }

void jit_emit_tick_add(JitEmit& e, u32 count) {
    if (count == 0 || count > 127)
        { e.oom = true; return; }
    emit_u8(e, 0x83); emit_u8(e, 0x86); emit_u32(e, JIT_OFF_TICK_DELTA);
    emit_u8(e, (u8)count);
}

u32 emit_jcc32(JitEmit& e, u8 cc) {
    emit_u8(e, 0x0F); emit_u8(e, cc);
    u32 p = e.len;
    emit_u32(e, 0);
    return p;
}

u32 emit_jmp32(JitEmit& e) {
    emit_u8(e, 0xE9);
    u32 p = e.len;
    emit_u32(e, 0);
    return p;
}

void emit_patch32(JitEmit& e, u32 pos) { patch_rel32(e, pos); }

// Phase 6c TB chaining: one 16-byte patchable exit site.
// Unpatched (tests, g_jit_chain_stub==null):
//   +0:  mov [rdx+JIT_OFF_NEXT_PC], target (10 B)
//   +10: mov eax, JIT_EXIT_NEXT_PC; ret (6 B)
// Unpatched (runtime, jmp miss stub):
//   +0:  mov [rdx+JIT_OFF_NEXT_PC], target (10 B)
//   +10: jmp rel32 stub (5 B) + nop (1 B)
// Patched (direct chain): mov eax, entry; jmp eax; int3 padding (16 B).

u8* g_jit_chain_stub = nullptr;

u32 jit_emit_chain_exit(JitEmit& e, u32 target_pc, JitChainInfo* info) {
    u32 off = e.len;
    emit_mov_rdx_disp32(e, JIT_OFF_NEXT_PC, target_pc);
    jit_emit_epilog(e, (u32)JIT_EXIT_NEXT_PC);
    if (e.oom)
        return off;
    if (e.len - off != JIT_CHAIN_SITE_SIZE) {
        e.oom = true;
        return off;
    }
    if (info && info->n < JitChainInfo::kMaxSites)
        info->sites[info->n++] = {off, target_pc};
    return off;
}

void jit_patch_chain_site_jmp_stub(u8* tb_base, u32 code_off, u8* stub) {
    if (!stub)
        return;
    u8* p = tb_base + code_off + 10;
    if (p[0] != 0xE9)
        return;
    intptr_t from = (intptr_t)(p + 4);
    intptr_t to = (intptr_t)stub;
    intptr_t delta = to - from;
    if (delta != (intptr_t)(s32)delta)
        return;
    s32 rel = (s32)delta;
    memcpy(p + 1, &rel, 4);
#ifdef _WIN32
    FlushInstructionCache(GetCurrentProcess(), p, 5);
#else
    __builtin___clear_cache((char*)p, (char*)p + 5);
#endif
}

void jit_patch_chain_site(u8* tb_base, u32 code_off, u8* chain_entry) {
    u8* p = tb_base + code_off;
    u32 target = (u32)(uintptr_t)chain_entry;
    p[0] = 0xB8;
    memcpy(p + 1, &target, 4);
    p[5] = 0xFF;
    p[6] = 0xE0;
    for (u32 i = 7; i < JIT_CHAIN_SITE_SIZE; i++)
        p[i] = 0xCC;
#ifdef _WIN32
    FlushInstructionCache(GetCurrentProcess(), p, JIT_CHAIN_SITE_SIZE);
#else
    __builtin___clear_cache((char*)p, (char*)p + JIT_CHAIN_SITE_SIZE);
#endif
}
