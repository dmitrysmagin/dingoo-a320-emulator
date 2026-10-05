#include "emit.h"

#include <cstddef>
#include <cstdio>
#include <cstring>

// Layout guards: emitter hardcodes these offsets.
static_assert(offsetof(JitState, gpr) == 0, "gpr base moved");
static_assert(sizeof(((JitState*)0)->gpr) == 128, "gpr size changed");

// Phase-1 JitState view: gpr[32] then hi/lo scratch slots. The full
// DYNAREC_PLAN CpuState grows later; for ALU-only TBs we need GPRs + HI/LO.
// They live in the dwords right after gpr[] (exit_code/exit_arg offsets).
// To avoid clobbering exit fields, the emitter spills hi/lo into two
// dedicated u32 slots appended via this wrapper in tests; in production the
// dispatcher provides a wider state. Offsets below match JIT_OFF_HI/LO.
struct EmitState {
    JitState base;
};

// ---- raw byte helpers ----

static void emit_u8(JitEmit& e, u8 b) {
    if (e.len < e.cap) e.buf[e.len++] = b;
    else e.oom = true;
}
static void emit_u32(JitEmit& e, u32 v) {
    emit_u8(e, (u8)(v & 0xFF)); emit_u8(e, (u8)((v >> 8) & 0xFF));
    emit_u8(e, (u8)((v >> 16) & 0xFF)); emit_u8(e, (u8)((v >> 24) & 0xFF));
}

// mov r32, [rdx+disp32] — load gpr[i]/hi/lo slot into host reg.
// Host regs used: EAX=0, ECX=1 (scratch). RDX=2 is the state base.
static void emit_load_slot(JitEmit& e, u32 host, u32 off) {
    emit_u8(e, 0x8B);                       // MOV r32, r/m32
    emit_u8(e, (u8)(0x80 | (host << 3) | 0x02));  // mod=10 reg=host r/m=010(RDX)
    emit_u32(e, off);
}
// mov [rdx+disp32], r32 — store host reg into slot.
static void emit_store_slot(JitEmit& e, u32 off, u32 host) {
    emit_u8(e, 0x89);                       // MOV r/m32, r32
    emit_u8(e, (u8)(0x80 | (host << 3) | 0x02));
    emit_u32(e, off);
}
static inline u32 slot_off(u32 reg) { return JIT_OFF_GPR + reg * 4; }

// prolog: state* (RDI or RCX) -> RDX. Emits both moves; harmless duplicate.
static void emit_prolog(JitEmit& e) {
    emit_u8(e, 0x48); emit_u8(e, 0x8B); emit_u8(e, 0xD7);  // mov rdx, rdi
    emit_u8(e, 0x48); emit_u8(e, 0x8B); emit_u8(e, 0xD1);  // mov rdx, rcx
}
// epilog: mov eax, exit; ret
static void emit_epilog(JitEmit& e, u32 exit_code) {
    emit_u8(e, 0xB8); emit_u32(e, exit_code);  // mov eax, imm32
    emit_u8(e, 0xC3);                          // ret
}

// r32, r/m32: 03 ADD, 2B SUB, 23 AND, 0B OR, 33 XOR, 3B CMP (mod=11)
static void emit_alu_reg(JitEmit& e, u8 opc, u32 dst_host, u32 src_host) {
    emit_u8(e, opc);
    emit_u8(e, (u8)(0xC0 | (dst_host << 3) | src_host));
}
// shift r/m32 by imm8: C1 /4 SHL, /5 SHR, /7 SAR (mod=11, EAX)
static void emit_shift_imm(JitEmit& e, u32 sub, u32 amt) {
    emit_u8(e, 0xC1);
    emit_u8(e, (u8)(0xC0 | (sub << 3) | 0x00));
    emit_u8(e, (u8)(amt & 0x1F));
}
// mov r32, imm32: B8+rd
static void emit_mov_imm(JitEmit& e, u32 host, u32 imm) {
    emit_u8(e, (u8)(0xB8 + host));
    emit_u32(e, imm);
}
// test r/m32, r32 (mod=11): 85 /r
static void emit_test(JitEmit& e, u32 a, u32 b) {
    emit_u8(e, 0x85);
    emit_u8(e, (u8)(0xC0 | (a << 3) | b));
}
// Conditional jump with rel8 patched later. Returns the offset of the
// rel8 byte so the caller can patch it to reach e.len. opc: 0x74 JE,
// 0x75 JNE, 0xEB JMP.
static u32 emit_jcc_begin(JitEmit& e, u8 opc) {
    emit_u8(e, opc);
    u32 rel_pos = e.len;
    emit_u8(e, 0x00);  // patched by emit_jcc_end
    return rel_pos;
}
static void emit_jcc_end(JitEmit& e, u32 rel_pos) {
    // rel8 = target - (rel_pos + 1). Caller must ensure < 127.
    u32 target = e.len;
    s32 rel = (s32)target - (s32)(rel_pos + 1);
    if (rel < -128 || rel > 127 || rel_pos >= e.cap) {
        e.oom = true;
        return;
    }
    e.buf[rel_pos] = (u8)(rel & 0xFF);
}
// cmovcc r32, r/m32 (0F 4x, mod=11): EAX = ECX if cc
static void emit_cmov(JitEmit& e, u8 cc, u32 dst_host, u32 src_host) {
    emit_u8(e, 0x0F); emit_u8(e, cc);
    emit_u8(e, (u8)(0xC0 | (dst_host << 3) | src_host));
}

// Decode EXT fields from the original word (MIPS32 spec):
// pos = bits 10:6 (sa), msb = bits 15:11 (rd). size = msb-pos+1.
static void ext_fields(s32 word, u32& pos, u32& size) {
    u32 w = (u32)word;
    pos = (w >> 6) & 0x1F;
    u32 msb = (w >> 11) & 0x1F;
    size = (msb >= pos) ? (msb - pos + 1) : 32;  // degenerate -> full (interp masks to 0-shift; emitter: copy)
}
// Decode INS fields: pos = bits 10:6, msb = bits 15:11 (rt holds msb per spec).
static void ins_fields(s32 word, u32& pos, u32& size) {
    ext_fields(word, pos, size);
}
static u32 mask32(u32 size) {
    if (size >= 32) return 0xFFFFFFFFu;
    if (size == 0) return 0u;
    return (size == 32) ? 0xFFFFFFFFu : ((1u << size) - 1u);
}

// ---- ALU op templates (EAX=dst accumulator, ECX=scratch) ----
// Convention per op: load operand(s) into EAX(,ECX), compute in EAX, store.

// r/m32, imm32: 81 /0 ADD, /5 SUB, /4 AND, /1 OR, /6 XOR, /7 CMP
static void emit_alu_imm(JitEmit& e, u32 sub, u32 imm) {
    emit_u8(e, 0x81);
    emit_u8(e, (u8)(0xC0 | (sub << 3) | 0x00));  // mod=11 reg=sub r/m=EAX
    emit_u32(e, imm);
}
// Emit one ALU op. SLLV/SRLV/SRAV never reach here (decoder stops).
static bool emit_one(JitEmit& e, const JitAluInsn& o) {
    switch (o.op) {
    case JIT_ALU_NOP:
        return true;
    case JIT_ALU_SLL:  // rd = rt << sa
        emit_load_slot(e, 0, slot_off(o.rt));
        emit_shift_imm(e, 4, o.sa);
        emit_store_slot(e, slot_off(o.rd), 0);
        return true;
    case JIT_ALU_SRL:  // rd = rt >> sa (logical)
        emit_load_slot(e, 0, slot_off(o.rt));
        emit_shift_imm(e, 5, o.sa);
        emit_store_slot(e, slot_off(o.rd), 0);
        return true;
    case JIT_ALU_SRA:  // rd = (s32)rt >> sa (arithmetic)
        emit_load_slot(e, 0, slot_off(o.rt));
        emit_shift_imm(e, 7, o.sa);
        emit_store_slot(e, slot_off(o.rd), 0);
        return true;
    case JIT_ALU_ADDU:  // rd = rs + rt (wrap, no trap)
        emit_load_slot(e, 0, slot_off(o.rs));
        emit_load_slot(e, 1, slot_off(o.rt));
        emit_alu_reg(e, 0x03, 0, 1);  // add eax, ecx
        emit_store_slot(e, slot_off(o.rd), 0);
        return true;
    case JIT_ALU_SUBU:  // rd = rs - rt
        emit_load_slot(e, 0, slot_off(o.rs));
        emit_load_slot(e, 1, slot_off(o.rt));
        emit_alu_reg(e, 0x2B, 0, 1);  // sub eax, ecx
        emit_store_slot(e, slot_off(o.rd), 0);
        return true;
    case JIT_ALU_AND:
        emit_load_slot(e, 0, slot_off(o.rs));
        emit_load_slot(e, 1, slot_off(o.rt));
        emit_alu_reg(e, 0x23, 0, 1);  // and eax, ecx
        emit_store_slot(e, slot_off(o.rd), 0);
        return true;
    case JIT_ALU_OR:
        emit_load_slot(e, 0, slot_off(o.rs));
        emit_load_slot(e, 1, slot_off(o.rt));
        emit_alu_reg(e, 0x0B, 0, 1);  // or eax, ecx
        emit_store_slot(e, slot_off(o.rd), 0);
        return true;
    case JIT_ALU_XOR:
        emit_load_slot(e, 0, slot_off(o.rs));
        emit_load_slot(e, 1, slot_off(o.rt));
        emit_alu_reg(e, 0x33, 0, 1);  // xor eax, ecx
        emit_store_slot(e, slot_off(o.rd), 0);
        return true;
    case JIT_ALU_NOR:  // rd = ~(rs | rt)
        emit_load_slot(e, 0, slot_off(o.rs));
        emit_load_slot(e, 1, slot_off(o.rt));
        emit_alu_reg(e, 0x0B, 0, 1);  // or eax, ecx
        emit_u8(e, 0xF7); emit_u8(e, 0xD0);  // not eax
        emit_store_slot(e, slot_off(o.rd), 0);
        return true;
    case JIT_ALU_SLT: {  // rd = (s32)rs < (s32)rt
        emit_load_slot(e, 0, slot_off(o.rs));  // eax = rs
        emit_load_slot(e, 1, slot_off(o.rt));  // ecx = rt
        emit_alu_reg(e, 0x3B, 1, 0);  // cmp ecx, eax
        emit_mov_imm(e, 0, 0);        // eax = 0
        emit_u8(e, 0x0F); emit_u8(e, 0x9C); emit_u8(e, 0xC0);  // setl al
        emit_store_slot(e, slot_off(o.rd), 0);
        return true;
    }
    case JIT_ALU_SLTU: {  // rd = rs < rt (unsigned)
        emit_load_slot(e, 0, slot_off(o.rs));
        emit_load_slot(e, 1, slot_off(o.rt));
        emit_alu_reg(e, 0x3B, 1, 0);  // cmp ecx, eax
        emit_mov_imm(e, 0, 0);
        emit_u8(e, 0x0F); emit_u8(e, 0x92); emit_u8(e, 0xC0);  // setb al
        emit_store_slot(e, slot_off(o.rd), 0);
        return true;
    }
    case JIT_ALU_MOVZ: {  // if (rt==0) rd = rs
        emit_load_slot(e, 1, slot_off(o.rt));  // ecx = rt
        emit_test(e, 1, 1);                    // test ecx, ecx (ZF=1 iff rt==0)
        u32 rel = emit_jcc_begin(e, 0x75);     // jne -> skip
        emit_load_slot(e, 0, slot_off(o.rs));  // eax = rs
        emit_store_slot(e, slot_off(o.rd), 0);
        emit_jcc_end(e, rel);
        return true;
    }
    case JIT_ALU_MOVN: {  // if (rt!=0) rd = rs
        emit_load_slot(e, 1, slot_off(o.rt));
        emit_test(e, 1, 1);
        u32 rel = emit_jcc_begin(e, 0x74);     // je -> skip
        emit_load_slot(e, 0, slot_off(o.rs));
        emit_store_slot(e, slot_off(o.rd), 0);
        emit_jcc_end(e, rel);
        return true;
    }
    default:
        break;
    case JIT_ALU_MFHI:  // rd = hi
        emit_load_slot(e, 0, JIT_OFF_HI);
        emit_store_slot(e, slot_off(o.rd), 0);
        return true;
    case JIT_ALU_MTHI:  // hi = rs
        emit_load_slot(e, 0, slot_off(o.rs));
        emit_store_slot(e, JIT_OFF_HI, 0);
        return true;
    case JIT_ALU_MFLO:  // rd = lo
        emit_load_slot(e, 0, JIT_OFF_LO);
        emit_store_slot(e, slot_off(o.rd), 0);
        return true;
    case JIT_ALU_MTLO:  // lo = rs
        emit_load_slot(e, 0, slot_off(o.rs));
        emit_store_slot(e, JIT_OFF_LO, 0);
        return true;
    case JIT_ALU_MULT: {  // {hi,lo} = (s64)(s32)rs * (s32)rt
        emit_u8(e, 0x48); emit_u8(e, 0x63); emit_u8(e, 0x82); emit_u32(e, slot_off(o.rs));  // movsxd rax,[rdx+rs]
        emit_u8(e, 0x48); emit_u8(e, 0x63); emit_u8(e, 0x8A); emit_u32(e, slot_off(o.rt));  // movsxd rcx,[rdx+rt]
        emit_u8(e, 0x48); emit_u8(e, 0x0F); emit_u8(e, 0xAF); emit_u8(e, 0xC1);              // imul rax, rcx
        emit_u8(e, 0x89); emit_u8(e, 0x82); emit_u32(e, JIT_OFF_LO);                        // mov [rdx+LO], eax
        emit_u8(e, 0x48); emit_u8(e, 0xC1); emit_u8(e, 0xE8); emit_u8(e, 0x20);              // shr rax, 32
        emit_u8(e, 0x89); emit_u8(e, 0x82); emit_u32(e, JIT_OFF_HI);                        // mov [rdx+HI], eax
        return true;
    }
    case JIT_ALU_MULTU: {  // {hi,lo} = (u64)rs * rt
        // WARNING: x86 MUL/DIV write EDX — but RDX holds JitState*. Save it.
        emit_load_slot(e, 0, slot_off(o.rs));  // eax = rs
        emit_load_slot(e, 1, slot_off(o.rt));  // ecx = rt
        emit_u8(e, 0x52);                      // push rdx (save base)
        emit_u8(e, 0xF7); emit_u8(e, 0xE1);    // mul ecx (edx:eax = eax*ecx)
        emit_u8(e, 0x89); emit_u8(e, 0xD1);    // mov ecx, edx (hi)
        emit_u8(e, 0x5A);                      // pop rdx (restore base)
        emit_store_slot(e, JIT_OFF_LO, 0);     // lo = eax
        emit_store_slot(e, JIT_OFF_HI, 1);     // hi = ecx
        return true;
    }
    case JIT_ALU_MUL: {  // rd = rs * rt (low 32)
        emit_load_slot(e, 0, slot_off(o.rs));
        emit_load_slot(e, 1, slot_off(o.rt));
        emit_u8(e, 0x0F); emit_u8(e, 0xAF); emit_u8(e, 0xC1);  // imul eax, ecx
        emit_store_slot(e, slot_off(o.rd), 0);
        return true;
    }
    case JIT_ALU_DIV: {  // if (rt) { lo = (s32)rs/(s32)rt; hi = % }
        // x86 IDIV traps on div-by-zero AND INT_MIN/-1; interp skips div0
        // and wraps INT_MIN/-1. Guard both with patched jumps.
        // EDX problem: idiv clobbers edx (upper half of the RDX state base).
        // Fix: spill the full 64-bit base on the stack AROUND the divide.
        // Stack discipline: push rdx saves all 8 bytes; after idiv we need
        // quot/rem in eax/ecx AND rdx restored. Do:
        //   push rdx            # [base64]
        //   mov eax,rs; cdq; idiv ecx   # eax=quot edx=rem
        //   mov [rsp+4],eax     # stash quot in HIGH half of saved slot
        //   mov eax,edx         # eax=rem
        //   xchg eax,[rsp]      # eax=base_lo, [rsp]=rem  (32-bit xchg, keeps high half=quot)
        //   mov edx,[rsp+4]     # edx=quot... then mov ecx,edx? Let's recount:
        // After xchg: eax=base_lo, [rsp]=rem, [rsp+4]=quot.
        //   mov edx,eax         # edx=base_lo (rdx high half still rem!!) — NO.
        // The 64-bit base needs both halves. Since only the low 32 bits of
        // the base are ever nonzero (heap pointer < 4 GB), but correctness:
        // store base_hi too. Correct sequence with two stack slots:
        //   push rdx            # [base_lo][base_hi]
        //   ... idiv ...
        //   mov [rsp+8],eax     # quot above base
        //   mov [rsp+12],edx    # rem above quot
        //   pop rdx             # restore base (pops base_lo+base_hi)
        //   mov eax,[rsp]?? — stack now [quot][rem]; pop order: pop rcx(quot)? ecx is 32-bit part of rcx — `pop rcx` is 64-bit, clobbers upper half (fine, nothing live there).
        //   pop rcx -> rcx=quot64; mov eax,ecx... then pop rax=rem64.
        // Final:
        //   push rdx
        //   mov eax,rs; cdq; idiv ecx
        //   push rax (quot64, zero-extended) — need `push rax` (50+0=0x50)
        //   mov eax,edx (rem); push rax (rem64)
        //   pop rdx?? order: stack=[base][quot][rem] -> pop rdx=rem! wrong,
        //   need rem->rax, quot->rcx, then base->rdx:
        //   pop rax (rem) | pop rcx (quot) | pop rdx (base)
        //   mov ecx,eax?? no: after pops, rax=rem64, rcx=quot64, rdx=base. Then:
        //   mov ecx,eax is wrong. Do: mov eax,ecx (quot->eax) BEFORE pops?
        // Simplest correct order:
        //   push rdx            # [base]
        //   mov eax,rs; cdq; idiv ecx   # eax=quot edx=rem
        //   mov ecx,eax         # ecx=quot (saved in reg)
        //   mov eax,edx         # eax=rem (saved in reg)
        //   pop rdx             # restore base — edx CLOBBERED? No: pop rdx
        //     writes full rdx = base, destroying rem in eax? No — rem is in
        //     EAX, base goes to RDX. eax untouched by pop rdx. CORRECT!
        // Wait — that's the whole fix: keep quot/rem in eax/ecx across pop.
        emit_load_slot(e, 1, slot_off(o.rt));  // ecx = rt (divisor)
        emit_test(e, 1, 1);
        u32 rel_div0 = emit_jcc_begin(e, 0x74);  // je -> end (rt==0: skip)
        emit_load_slot(e, 0, slot_off(o.rs));  // eax = rs
        emit_u8(e, 0x3D); emit_u32(e, 0x80000000u);  // cmp eax, INT_MIN
        u32 rel_notmin = emit_jcc_begin(e, 0x75);  // jne -> do_divide
        emit_u8(e, 0x81); emit_u8(e, 0xF9); emit_u32(e, 0xFFFFFFFFu);  // cmp ecx, -1
        u32 rel_notneg1 = emit_jcc_begin(e, 0x75);  // jne -> do_divide
        emit_mov_imm(e, 0, 0x80000000u);       // lo = INT_MIN (MIPS wrap)
        emit_store_slot(e, JIT_OFF_LO, 0);
        emit_mov_imm(e, 0, 0);                 // hi = 0
        emit_store_slot(e, JIT_OFF_HI, 0);
        u32 rel_wrap = emit_jcc_begin(e, 0xEB);  // jmp -> end
        // do_divide:
        emit_jcc_end(e, rel_notmin);
        emit_jcc_end(e, rel_notneg1);
        // pop-rdx fix: quot->ecx, rem->eax survive `pop rdx` untouched.
        emit_u8(e, 0x52);                      // push rdx (save 64-bit base)
        emit_load_slot(e, 0, slot_off(o.rs));  // eax = rs
        emit_u8(e, 0x99);                      // cdq
        emit_u8(e, 0xF7); emit_u8(e, 0xF9);    // idiv ecx (eax=quot, edx=rem)
        emit_u8(e, 0x89); emit_u8(e, 0xC1);    // mov ecx, eax (quot->ecx)
        emit_u8(e, 0x89); emit_u8(e, 0xD0);    // mov eax, edx (rem->eax)
        emit_u8(e, 0x5A);                      // pop rdx (restore base; eax/ecx intact)
        emit_store_slot(e, JIT_OFF_LO, 1);     // lo = ecx (quotient)
        emit_store_slot(e, JIT_OFF_HI, 0);     // hi = eax (remainder)
        emit_jcc_end(e, rel_wrap);
        emit_jcc_end(e, rel_div0);
        return true;
    }
    case JIT_ALU_DIVU: {  // if (rt) { lo = rs/rt; hi = rs%rt }
        emit_load_slot(e, 1, slot_off(o.rt));  // ecx = rt
        emit_test(e, 1, 1);
        u32 rel = emit_jcc_begin(e, 0x74);     // je -> end (rt==0: skip)
        // Same pop-rdx fix as DIV.
        emit_u8(e, 0x52);                      // push rdx
        emit_load_slot(e, 0, slot_off(o.rs));  // eax = rs
        emit_u8(e, 0x31); emit_u8(e, 0xD2);    // xor edx, edx
        emit_u8(e, 0xF7); emit_u8(e, 0xF1);    // div ecx (eax=quot, edx=rem)
        emit_u8(e, 0x89); emit_u8(e, 0xC1);    // mov ecx, eax (quot->ecx)
        emit_u8(e, 0x89); emit_u8(e, 0xD0);    // mov eax, edx (rem->eax)
        emit_u8(e, 0x5A);                      // pop rdx
        emit_store_slot(e, JIT_OFF_LO, 1);
        emit_store_slot(e, JIT_OFF_HI, 0);
        emit_jcc_end(e, rel);
        return true;
    }
    case JIT_ALU_ADDI:  // rt = rs + sext16(imm), wrap (interp: no trap)
        emit_load_slot(e, 0, slot_off(o.rs));
        emit_alu_imm(e, 0, (u32)(s32)o.imm);  // add eax, imm32
        emit_store_slot(e, slot_off(o.rt), 0);
        return true;
    case JIT_ALU_ADDIU:
        emit_load_slot(e, 0, slot_off(o.rs));
        emit_alu_imm(e, 0, (u32)(s32)o.imm);
        emit_store_slot(e, slot_off(o.rt), 0);
        return true;
    case JIT_ALU_SLTI: {  // rt = (s32)rs < sext16(imm)
        emit_load_slot(e, 1, slot_off(o.rs));  // ecx = rs
        emit_u8(e, 0x81); emit_u8(e, 0xF9); emit_u32(e, (u32)(s32)o.imm);  // cmp ecx, imm
        emit_mov_imm(e, 0, 0);                  // eax = 0
        emit_u8(e, 0x0F); emit_u8(e, 0x9C); emit_u8(e, 0xC0);  // setl al
        // eax is already 0/1 (movzx unneeded: setl writes al, rest stays 0).
        emit_store_slot(e, slot_off(o.rt), 0);
        return true;
    }
    case JIT_ALU_SLTIU: {  // rt = rs < (u32)(s32)imm
        emit_load_slot(e, 1, slot_off(o.rs));  // ecx = rs
        emit_u8(e, 0x81); emit_u8(e, 0xF9); emit_u32(e, (u32)(s32)o.imm);  // cmp ecx, imm
        emit_mov_imm(e, 0, 0);
        emit_u8(e, 0x0F); emit_u8(e, 0x92); emit_u8(e, 0xC0);  // setb al
        emit_store_slot(e, slot_off(o.rt), 0);
        return true;
    }
    case JIT_ALU_ANDI:  // rt = rs & zero_ext(imm)
        emit_load_slot(e, 0, slot_off(o.rs));
        emit_alu_imm(e, 4, o.uimm);  // and eax, imm
        emit_store_slot(e, slot_off(o.rt), 0);
        return true;
    case JIT_ALU_ORI:
        emit_load_slot(e, 0, slot_off(o.rs));
        emit_alu_imm(e, 1, o.uimm);  // or eax, imm
        emit_store_slot(e, slot_off(o.rt), 0);
        return true;
    case JIT_ALU_XORI:
        emit_load_slot(e, 0, slot_off(o.rs));
        emit_alu_imm(e, 6, o.uimm);  // xor eax, imm
        emit_store_slot(e, slot_off(o.rt), 0);
        return true;
    case JIT_ALU_LUI:  // rt = imm << 16
        emit_mov_imm(e, 0, o.uimm << 16);
        emit_store_slot(e, slot_off(o.rt), 0);
        return true;
    case JIT_ALU_EXT: {  // rt = (rs >> pos) & mask(size), size=msb-pos+1
        u32 pos, size;
        ext_fields(o.imm, pos, size);
        emit_load_slot(e, 0, slot_off(o.rs));  // eax = rs
        if (pos)
            emit_shift_imm(e, 5, pos);          // shr eax, pos
        u32 m = mask32(size);
        if (m != 0xFFFFFFFFu)
            emit_alu_imm(e, 4, m);              // and eax, mask
        emit_store_slot(e, slot_off(o.rt), 0);
        return true;
    }
    case JIT_ALU_INS: {  // rt = (rt&~(m<<pos)) | ((rs&m)<<pos)
        u32 pos, size;
        ins_fields(o.imm, pos, size);
        u32 m = mask32(size);
        // ecx = rs; and ecx, m; shl ecx, pos  (open-coded: emit_alu_imm is EAX-only)
        emit_load_slot(e, 1, slot_off(o.rs));  // ecx = rs
        if (m != 0xFFFFFFFFu) {
            emit_u8(e, 0x81); emit_u8(e, 0xE1); emit_u32(e, m);  // and ecx, m
        }
        if (pos)
            { emit_u8(e, 0xC1); emit_u8(e, 0xE1); emit_u8(e, (u8)pos); }  // shl ecx, pos
        // eax = rt; clear the field; or in the new bits.
        u32 field = (size >= 32) ? 0xFFFFFFFFu : (m << pos);
        emit_load_slot(e, 0, slot_off(o.rt));  // eax = rt
        if (field != 0xFFFFFFFFu) {
            emit_u8(e, 0x81); emit_u8(e, 0xE0); emit_u32(e, ~field);  // and eax, ~field
        } else {
            emit_mov_imm(e, 0, 0);  // field covers all: eax = 0 first
        }
        emit_alu_reg(e, 0x0B, 0, 1);  // or eax, ecx
        emit_store_slot(e, slot_off(o.rt), 0);
        return true;
    }
    case JIT_ALU_CLZ:  // (SPECIAL2-only; decoder never emits here — safety net)
    case JIT_ALU_CLO:
        return false;
    }
    return false;
}

u32 jit_compile_tb(const JitTbPlan& plan, u8* buf, u32 cap, u32 exit_code) {
    if (!buf && cap)
        return 0;
    JitEmit e;
    e.buf = buf; e.cap = cap; e.len = 0; e.oom = false;
    emit_prolog(e);
    for (u32 i = 0; i < plan.count; i++) {
        if (!emit_one(e, plan.ops[i]) || e.oom)
            return 0;
    }
    emit_epilog(e, exit_code);
    if (e.oom || e.len > cap)
        return 0;
    return e.len;
}

// ---- reference model (plain C++, mirrors cpu.cpp) ----

static u32 ref_clz(u32 v) {
    if (v == 0) return 32;
    u32 c = 0;
    while ((v & 0x80000000u) == 0) { v <<= 1; c++; }
    return c;
}

void jit_run_reference(const JitTbPlan& plan, u32 regs[32], u32* hi, u32* lo) {
    u32 h = hi ? *hi : 0, l = lo ? *lo : 0;
    for (u32 i = 0; i < plan.count; i++) {
        const JitAluInsn& o = plan.ops[i];
        switch (o.op) {
        case JIT_ALU_NOP: break;
        case JIT_ALU_SLL: regs[o.rd] = regs[o.rt] << (o.sa & 0x1F); break;
        case JIT_ALU_SRL: regs[o.rd] = regs[o.rt] >> (o.sa & 0x1F); break;
        case JIT_ALU_SRA: regs[o.rd] = (u32)((s32)regs[o.rt] >> (o.sa & 0x1F)); break;
        case JIT_ALU_ADDU: regs[o.rd] = regs[o.rs] + regs[o.rt]; break;
        case JIT_ALU_SUBU: regs[o.rd] = regs[o.rs] - regs[o.rt]; break;
        case JIT_ALU_AND: regs[o.rd] = regs[o.rs] & regs[o.rt]; break;
        case JIT_ALU_OR: regs[o.rd] = regs[o.rs] | regs[o.rt]; break;
        case JIT_ALU_XOR: regs[o.rd] = regs[o.rs] ^ regs[o.rt]; break;
        case JIT_ALU_NOR: regs[o.rd] = ~(regs[o.rs] | regs[o.rt]); break;
        case JIT_ALU_SLT: regs[o.rd] = (s32)regs[o.rs] < (s32)regs[o.rt] ? 1 : 0; break;
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
            if (regs[o.rt]) {
                // Match x86-safe wrap: INT_MIN/-1 -> lo=INT_MIN, hi=0.
                if (regs[o.rs] == 0x80000000u && regs[o.rt] == 0xFFFFFFFFu) {
                    l = 0x80000000u; h = 0;
                } else {
                    l = (u32)((s32)regs[o.rs] / (s32)regs[o.rt]);
                    h = (u32)((s32)regs[o.rs] % (s32)regs[o.rt]);
                }
            }
            break;
        case JIT_ALU_DIVU:
            if (regs[o.rt]) { l = regs[o.rs] / regs[o.rt]; h = regs[o.rs] % regs[o.rt]; }
            break;
        case JIT_ALU_MUL: regs[o.rd] = regs[o.rs] * regs[o.rt]; break;
        case JIT_ALU_ADDI: regs[o.rt] = regs[o.rs] + (u32)o.imm; break;
        case JIT_ALU_ADDIU: regs[o.rt] = regs[o.rs] + (u32)o.imm; break;
        case JIT_ALU_SLTI: regs[o.rt] = (s32)regs[o.rs] < o.imm ? 1 : 0; break;
        case JIT_ALU_SLTIU: regs[o.rt] = regs[o.rs] < (u32)o.imm ? 1 : 0; break;
        case JIT_ALU_ANDI: regs[o.rt] = regs[o.rs] & o.uimm; break;
        case JIT_ALU_ORI: regs[o.rt] = regs[o.rs] | o.uimm; break;
        case JIT_ALU_XORI: regs[o.rt] = regs[o.rs] ^ o.uimm; break;
        case JIT_ALU_LUI: regs[o.rt] = o.uimm << 16; break;
        case JIT_ALU_EXT: {
            u32 pos, size;
            ext_fields(o.imm, pos, size);
            u32 m = mask32(size);
            regs[o.rt] = (regs[o.rs] >> pos) & m;
            break;
        }
        case JIT_ALU_INS: {
            u32 pos, size;
            ins_fields(o.imm, pos, size);
            u32 m = mask32(size);
            u32 field = (size >= 32) ? 0xFFFFFFFFu : (m << pos);
            regs[o.rt] = (regs[o.rt] & ~field) | ((regs[o.rs] & m) << pos);
            break;
        }
        case JIT_ALU_CLZ: regs[o.rd] = ref_clz(regs[o.rs]); break;
        case JIT_ALU_CLO: regs[o.rd] = ref_clz(~regs[o.rs]); break;
        }
        regs[0] = 0;  // $0 hardwired (safety; decoder already folds $0 dests)
    }
    if (hi) *hi = h;
    if (lo) *lo = l;
}


