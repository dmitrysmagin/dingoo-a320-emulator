// Stub host codegen (JIT_HOST=arm64|x86): same API as x64/emit.h, no bytes emitted.
#include "emit.h"

u8* g_jit_chain_stub = nullptr;

u32 jit_compile_tb(const JitTbPlan& plan, u8* buf, u32 cap, u32 exit_code, u32 entry_pc) {
    (void)plan; (void)buf; (void)cap; (void)exit_code; (void)entry_pc;
    return 0;
}

u32 jit_compile_branch_tb(const JitTbPlan& plan, u32 branch_idx, u32 entry_pc,
                          u8* buf, u32 cap, JitChainInfo* chain) {
    (void)plan; (void)branch_idx; (void)entry_pc; (void)buf; (void)cap; (void)chain;
    return 0;
}

u32 jit_compile_got_tb(u32 entry_pc, s32 got_index, u8* buf, u32 cap) {
    (void)entry_pc; (void)got_index; (void)buf; (void)cap;
    return 0;
}

void jit_emit_prolog(JitEmit& e, u32 entry_pc, u32 insn_count) {
    (void)entry_pc; (void)insn_count;
    (void)e;
}

void jit_emit_epilog(JitEmit& e, u32 exit_code) { (void)e; (void)exit_code; }

void jit_emit_tick_add(JitEmit& e, u32 count) { (void)e; (void)count; }

u32 emit_jcc32(JitEmit& e, u8 cc) { (void)e; (void)cc; return 0; }

u32 emit_jmp32(JitEmit& e) { (void)e; return 0; }

void emit_patch32(JitEmit& e, u32 pos) { (void)e; (void)pos; }

bool jit_emit_op(JitEmit& e, const JitAluInsn& op, u32 op_idx) {
    (void)e; (void)op; (void)op_idx;
    return false;
}

bool emit_mem_op(JitEmit& e, const JitAluInsn& op, u32 op_idx) {
    (void)e; (void)op; (void)op_idx;
    return false;
}

bool emit_cop_op(JitEmit& e, const JitAluInsn& op) {
    (void)e; (void)op;
    return false;
}

u32 jit_emit_chain_exit(JitEmit& e, u32 target_pc, JitChainInfo* info) {
    (void)e; (void)target_pc; (void)info;
    return 0;
}

void jit_patch_chain_site(u8* tb_base, u32 code_off, u8* chain_entry) {
    (void)tb_base; (void)code_off; (void)chain_entry;
}

void jit_patch_chain_site_jmp_stub(u8* tb_base, u32 code_off, u8* stub) {
    (void)tb_base; (void)code_off; (void)stub;
}

void emit_mov_imm(JitEmit& e, u32 host_reg, u32 imm) {
    (void)e; (void)host_reg; (void)imm;
}

void emit_mov_rdx_disp32(JitEmit& e, u32 disp32, u32 imm) {
    (void)e; (void)disp32; (void)imm;
}

u32 compute_branch_target(const JitAluInsn& op, u32 pc) {
    (void)op; (void)pc;
    return 0xFFFFFFFFu;
}

void emit_branch_exit(JitEmit& e, const JitAluInsn& op, u32 target) {
    (void)e; (void)op; (void)target;
}
