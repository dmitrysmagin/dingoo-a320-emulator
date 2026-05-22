#include "cpu.h"
#include <cstdio>
#include <cstring>
#include <ctime>

// Global register access for syscall dispatch
u32 g_cpu_regs[32];
u32 g_detected_fb_addr = 0;
u32 g_cpu_pc;
u32 g_cpu_hi;
u32 g_cpu_lo;

// Sign extend helpers
static inline s32 sext16(u32 v) { return (s32)(s16)v; }
static inline s32 sext26(u32 v) { return (s32)(v << 6) >> 6; }

void CPU::reset() {
    memset(regs, 0, sizeof(regs));
    pc = 0;
    hi = lo = 0;
    llbit = 0;
    cop0.reset();
    mxu.reset();
    running = true;
    insn_count = 0;
    m_trace_idx = 0;
    memset(m_trace_pc, 0, sizeof(m_trace_pc));
    memset(m_trace_insn, 0, sizeof(m_trace_insn));
}

u32 CPU::fetch() {
    // OS area (0x80000000-0x809FFFFF) has no loaded code - return JR $ra to skip
    if (pc >= 0x80000000 && pc < 0x80A00000) {
        static bool warned = false;
        if (!warned) {
            printf("[CPU] PC in OS area 0x%08X - returning JR $ra\n", pc);
            warned = true;
        }
        return 0x03E00008;  // JR $ra
    }
    return mem->read_u32(pc);
}

void CPU::raise_exception(u32 code) {
    cop0.regs.cause = (cop0.regs.cause & ~0x7C) | (code << 2);
    cop0.regs.epc = pc - 4;
    cop0.regs.bad_vaddr = pc;
    printf("[EXCEPTION] code=%u at PC=0x%08X\n", code, pc);
    running = false;
}

void CPU::exec_special(u32 insn) {
    int rs = (insn >> 21) & 0x1F;
    int rt = (insn >> 16) & 0x1F;
    int rd = (insn >> 11) & 0x1F;
    int sa = (insn >> 6) & 0x1F;
    int func = insn & 0x3F;

    switch (func) {
    case 0x00: if (rd) regs[rd] = regs[rt] << sa; break;
    case 0x02: if (rd) regs[rd] = regs[rt] >> sa; break;
    case 0x03: if (rd) regs[rd] = (u32)((s32)regs[rt] >> sa); break;
    case 0x04: if (rd) regs[rd] = regs[rt] << (regs[rs] & 0x1F); break;
    case 0x06: if (rd) regs[rd] = regs[rt] >> (regs[rs] & 0x1F); break;
    case 0x07: if (rd) regs[rd] = (u32)((s32)regs[rt] >> (regs[rs] & 0x1F)); break;
    case 0x08: pc = regs[rs]; if (rs == 31 && regs[31] == 0) pc = APP_MAIN_ADDR; break;
    case 0x09: regs[rd] = pc + 4; pc = regs[rs]; break;  // JALR
    case 0x0A: if (rd && regs[rt] == 0) regs[rd] = regs[rs]; break;  // MOVZ
    case 0x0B: if (rd && regs[rt] != 0) regs[rd] = regs[rs]; break;  // MOVN
    case 0x0C: raise_exception(EXC_SYS); break;
    case 0x0D: raise_exception(EXC_BP); break;
    case 0x0F: break;  // SYNC
    case 0x10: if (rd) regs[rd] = hi; break;
    case 0x11: hi = regs[rs]; break;
    case 0x12: if (rd) regs[rd] = lo; break;
    case 0x13: lo = regs[rs]; break;
    case 0x18: { s64 r = (s64)(s32)regs[rs] * (s64)(s32)regs[rt]; lo = (u32)r; hi = (u32)(r >> 32); } break;
    case 0x19: { u64 r = (u64)regs[rs] * (u64)regs[rt]; lo = (u32)r; hi = (u32)(r >> 32); } break;
    case 0x1A: if (regs[rt]) { lo = (u32)((s32)regs[rs] / (s32)regs[rt]); hi = (u32)((s32)regs[rs] % (s32)regs[rt]); } break;
    case 0x1B: if (regs[rt]) { lo = regs[rs] / regs[rt]; hi = regs[rs] % regs[rt]; } break;
    case 0x20: if (rd) { s64 r = (s64)(s32)regs[rs] + (s64)(s32)regs[rt]; if (r > INT32_MAX || r < INT32_MIN) { raise_exception(EXC_OV); return; } regs[rd] = (u32)r; } break;
    case 0x21: if (rd) regs[rd] = regs[rs] + regs[rt]; break;
    case 0x22: if (rd) { s64 r = (s64)(s32)regs[rs] - (s64)(s32)regs[rt]; if (r > INT32_MAX || r < INT32_MIN) { raise_exception(EXC_OV); return; } regs[rd] = (u32)r; } break;
    case 0x23: if (rd) regs[rd] = regs[rs] - regs[rt]; break;
    case 0x24: if (rd) regs[rd] = regs[rs] & regs[rt]; break;
    case 0x25: if (rd) regs[rd] = regs[rs] | regs[rt]; break;
    case 0x26: if (rd) regs[rd] = regs[rs] ^ regs[rt]; break;
    case 0x27: if (rd) regs[rd] = ~(regs[rs] | regs[rt]); break;
    case 0x2A: if (rd) regs[rd] = (s32)regs[rs] < (s32)regs[rt] ? 1 : 0; break;
    case 0x2B: if (rd) regs[rd] = regs[rs] < regs[rt] ? 1 : 0; break;
    default:
        // Non-standard func codes (possibly MXU extensions) - treated as NOP
        break;
    }
}

void CPU::exec_special2(u32 insn) {
    int func = insn & 0x3F;
    switch (func) {
    case 0x00: case 0x02: {
        int rs = (insn >> 21) & 0x1F;
        int rt = (insn >> 16) & 0x1F;
        int rd = (insn >> 11) & 0x1F;
        if (rd) regs[rd] = regs[rs] * regs[rt];
        break;
    }
    case 0x20: case 0x21: case 0x22: case 0x23: {
        int rs = (insn >> 21) & 0x1F;
        int rt = (insn >> 16) & 0x1F;
        u64 acc = ((u64)hi << 32) | lo;
        s64 result;
        if (func == 0x20) result = (s64)acc + (s64)(s32)regs[rs] * (s64)(s32)regs[rt];
        else if (func == 0x21) result = (s64)acc + (s64)regs[rs] * (s64)regs[rt];
        else if (func == 0x22) result = (s64)acc - (s64)(s32)regs[rs] * (s64)(s32)regs[rt];
        else result = (s64)acc - (s64)regs[rs] * (s64)regs[rt];
        lo = (u32)(result & 0xFFFFFFFF);
        hi = (u32)((result >> 32) & 0xFFFFFFFF);
        break;
    }
    case 0x24: {
        int rs = (insn >> 21) & 0x1F;
        int rd = (insn >> 11) & 0x1F;
        if (regs[rs] == 0) regs[rd] = 32;
        else { u32 v = regs[rs]; int c = 0; while ((v & 0x80000000) == 0) { v <<= 1; c++; } regs[rd] = c; }
        break;
    }
    default:
        printf("[CPU] SPECIAL2 unknown func=0x%02X at PC=0x%08X insn=0x%08X\n", func, pc - 4, insn);
        raise_exception(EXC_RI);
        break;
    }
}

void CPU::exec_special3(u32 insn) {
    int func = insn & 0x3F;
    int rt = (insn >> 16) & 0x1F;
    int rs = (insn >> 21) & 0x1F;
    int rd = (insn >> 11) & 0x1F;
    int sa = (insn >> 6) & 0x1F;

    if (func == 0x00) {
        int pos = sa;
        int size = rd + 1;
        regs[rt] = (regs[rs] >> pos) & ((1u << size) - 1);
    } else if (func == 0x04) {
        int pos = sa;
        int size = rd + 1;
        u32 mask = ((1u << size) - 1) << pos;
        regs[rt] = (regs[rt] & ~mask) | ((regs[rs] & ((1u << size) - 1)) << pos);
    } else {
        printf("[CPU] SPECIAL3 unknown func=0x%02X at PC=0x%08X insn=0x%08X\n", func, pc - 4, insn);
        raise_exception(EXC_RI);
    }
}

void CPU::execute(u32 insn) {
    int opcode = (insn >> 26) & 0x3F;
    int rs = (insn >> 21) & 0x1F;
    int rt = (insn >> 16) & 0x1F;
    int rd = (insn >> 11) & 0x1F;
    s32 imm = (s32)sext16(insn & 0xFFFF);
    u32 uimm = insn & 0xFFFF;

    switch (opcode) {
    case 0x00: exec_special(insn); break;
    case 0x01: {
        int rt_field = (insn >> 16) & 0x1F;
        s32 offset = sext16(insn & 0xFFFF);
        switch (rt_field) {
        case 0x00: if ((s32)regs[rs] < 0) pc = pc + (offset << 2); break;  // BLTZ
        case 0x01: if ((s32)regs[rs] >= 0) pc = pc + (offset << 2); break;  // BGEZ
        case 0x10: regs[31] = pc + 4; if ((s32)regs[rs] < 0) pc = pc + (offset << 2); break;  // BLTZAL
        case 0x11: regs[31] = pc + 4; if ((s32)regs[rs] >= 0) pc = pc + (offset << 2); break;  // BGEZAL
        }
        break;
    }
    case 0x02: {
        u32 target = (insn & 0x03FFFFFF) << 2;
        pc = (pc & 0xF0000000) | target;
        break;
    }
    case 0x03: {
        regs[31] = pc + 4;  // save address AFTER delay slot (A+8)
        u32 target = (insn & 0x03FFFFFF) << 2;
        pc = (pc & 0xF0000000) | target;
        break;
    }
    case 0x04: if (regs[rs] == regs[rt]) pc = pc + (imm << 2); break;  // BEQ
    case 0x05: if (regs[rs] != regs[rt]) pc = pc + (imm << 2); break;  // BNE
    case 0x06: if ((s32)regs[rs] <= 0) pc = pc + (imm << 2); break;  // BLEZ
    case 0x07: if ((s32)regs[rs] > 0) pc = pc + (imm << 2); break;  // BGTZ
    case 0x08: if (rt) { s64 r = (s64)(s32)regs[rs] + imm; if (r > INT32_MAX || r < INT32_MIN) { raise_exception(EXC_OV); return; } regs[rt] = (u32)r; } break;
    case 0x09: if (rt) regs[rt] = regs[rs] + (u32)imm; break;
    case 0x0A: if (rt) regs[rt] = (s32)regs[rs] < imm ? 1 : 0; break;
    case 0x0B: if (rt) regs[rt] = regs[rs] < (u32)imm ? 1 : 0; break;
    case 0x0C: if (rt) regs[rt] = regs[rs] & uimm; break;
    case 0x0D: if (rt) regs[rt] = regs[rs] | uimm; break;
    case 0x0E: if (rt) regs[rt] = regs[rs] ^ uimm; break;
    case 0x0F: if (rt) regs[rt] = uimm << 16; break;
    case 0x10: {
        int rs_field = (insn >> 21) & 0x1F;
        int rt_field = (insn >> 16) & 0x1F;
        int rd_field = (insn >> 11) & 0x1F;
        if (rs_field == 0x00) regs[rt_field] = cop0.mfc0(rd_field);
        else if (rs_field == 0x04) cop0.mtc0(rd_field, regs[rt_field]);
        else if (rs_field == 0x10 && rd_field == 0x18) {
            // ERET
            pc = cop0.regs.epc;
            cop0.regs.status |= 0x2;
        }
        else if (rs_field == 0x10) {
            switch (rd_field) {
            case 0x01: cop0.tlbr(); break;
            case 0x02: cop0.tlbwi(); break;
            case 0x06: cop0.tlbwr(); break;
            case 0x08: cop0.tlbp(); break;
            }
        }
        break;
    }
    case 0x11:
        printf("[CPU] COP1 (FPU) not implemented at PC=0x%08X insn=0x%08X\n", pc - 4, insn);
        raise_exception(EXC_RI);
        break;
    case 0x12: {
        int rs_field = (insn >> 21) & 0x1F;
        int rt_field = (insn >> 16) & 0x1F;
        int rd_field = (insn >> 11) & 0x1F;
        if (rs_field == 0x00) regs[rt_field] = mxu.mfc2(rd_field);
        else if (rs_field == 0x04) mxu.mtc2(rd_field, regs[rt_field]);
        else if (rs_field == 0x02) regs[rt_field] = mxu.cfc2(rd_field);
        else if (rs_field == 0x06) mxu.ctc2(rd_field, regs[rt_field]);
        else mxu.exec_custom(insn);
        break;
    }
    case 0x13:
        printf("[CPU] COP3 not implemented at PC=0x%08X\n", pc - 4);
        raise_exception(EXC_RI);
        break;
    case 0x14: if (regs[rs] == regs[rt]) pc = pc + (imm << 2); break;  // BEQL
    case 0x15: if (regs[rs] != regs[rt]) pc = pc + (imm << 2); break;  // BNEL
    case 0x16: if ((s32)regs[rs] <= 0) pc = pc + (imm << 2); break;  // BLEZL
    case 0x17: if ((s32)regs[rs] > 0) pc = pc + (imm << 2); break;  // BGTZL
    case 0x1C: exec_special2(insn); break;
    case 0x1F: exec_special3(insn); break;

    // Loads
    case 0x20: if (rt) regs[rt] = (u32)(s8)mem->read_u8(regs[rs] + imm); break;
    case 0x21: if (rt) regs[rt] = (u32)(s16)mem->read_u16(regs[rs] + imm); break;
    case 0x22: {  // LWL
        u32 addr = regs[rs] + imm;
        u32 aligned = addr & ~3;
        u32 shift = (addr & 3) * 8;
        u32 val = mem->read_u32(aligned);
        if (rt) regs[rt] = (regs[rt] & ~(0xFFFFFFFF >> shift)) | (val >> shift);
        break;
    }
    case 0x23: {
        u32 load_addr = regs[rs] + imm;
        u32 load_val = mem->read_u32(load_addr);
        if (rt == 31 && load_addr >= 0x80BFFFF0 && load_addr <= 0x80C00010) {
            printf("[LW-ra] 0x%08X -> 0x%08X at pc=0x%08X\n", load_addr, load_val, g_cpu_pc - 4);
        }
        if (rt) regs[rt] = load_val;
        if ((pc - 4) == 0x80A21E78) {
            u32 loaded = load_val & 0x1FFFFFFF;
            if (g_detected_fb_addr != load_val && loaded < RAM_SIZE) {
                g_detected_fb_addr = load_val;
            }
        }
        break;
    }
    case 0x24: if (rt) regs[rt] = mem->read_u8(regs[rs] + imm); break;
    case 0x25: if (rt) regs[rt] = mem->read_u16(regs[rs] + imm); break;
    case 0x26: {  // LWR
        u32 addr = regs[rs] + imm;
        u32 aligned = addr & ~3;
        u32 shift = (addr & 3) * 8;
        u32 val = mem->read_u32(aligned);
        if (rt) regs[rt] = (regs[rt] & (0xFFFFFFFF >> (32 - shift))) | (val << (32 - shift));
        break;
    }

    // Stores
    case 0x28: {
        u32 store_addr = regs[rs] + imm;
        mem->write_u8(store_addr, (u8)regs[rt]);

        break;
    }
    case 0x29: mem->write_u16(regs[rs] + imm, (u16)regs[rt]); break;
    case 0x2A: {  // SWL
        u32 addr = regs[rs] + imm;
        u32 aligned = addr & ~3;
        u32 shift = (addr & 3) * 8;
        u32 existing = mem->read_u32(aligned);
        mem->write_u32(aligned, (existing & ~(0xFFFFFFFF >> shift)) | (regs[rt] >> shift));
        break;
    }
    case 0x2B: {
        u32 store_addr = regs[rs] + imm;
        if (store_addr >= 0x80BFFFF0 && store_addr <= 0x80C00010) {
            printf("[SW-stack] 0x%08X = 0x%08X (r%d) at pc=0x%08X\n", store_addr, regs[rt], rt, g_cpu_pc - 4);
        }
        mem->write_u32(store_addr, regs[rt]);
        break;
    }
    case 0x2E: {  // SWR
        u32 addr = regs[rs] + imm;
        u32 aligned = addr & ~3;
        u32 shift = (addr & 3) * 8;
        u32 existing = mem->read_u32(aligned);
        mem->write_u32(aligned, (existing & (0xFFFFFFFF >> (32 - shift))) | (regs[rt] << (32 - shift)));
        break;
    }

    case 0x2F: break;  // CACHE (nop)
    case 0x30: {  // LL
        u32 addr = regs[rs] + imm;
        if (rt) regs[rt] = mem->read_u32(addr);
        llbit = 1;
        break;
    }
    case 0x31: {  // LWC1
        mem->read_u32(regs[rs] + imm);  // discard result
        break;
    }
    case 0x32: {  // LWC2
        u32 addr = regs[rs] + imm;
        mxu.mtc2(rt, mem->read_u32(addr));
        break;
    }
    case 0x33: {  // LWC3
        mem->read_u32(regs[rs] + imm);  // discard result
        break;
    }
    case 0x34: {  // SC
        u32 addr = regs[rs] + imm;
        if (llbit) { mem->write_u32(addr, regs[rt]); if (rt) regs[rt] = 1; }
        else { if (rt) regs[rt] = 0; }
        llbit = 0;
        break;
    }
    case 0x35: break;  // SWC1 (nop - no store)
    case 0x36: {  // SWC2
        u32 addr = regs[rs] + imm;
        mem->write_u32(addr, mxu.mfc2(rt));
        break;
    }
    case 0x37: break;  // SWC3 (nop - no store)

    default:
        printf("[CPU] Unknown opcode=0x%02X at PC=0x%08X insn=0x%08X\n", opcode, pc - 4, insn);
        raise_exception(EXC_RI);
        break;
    }
}

void CPU::trace_add(u32 pc_, u32 insn) {
    m_trace_pc[m_trace_idx] = pc_;
    m_trace_insn[m_trace_idx] = insn;
    m_trace_idx = (m_trace_idx + 1) % TRACE_SIZE;
}

void CPU::print_trace() {
    printf("\n=== Last %d instructions (most recent last) ===\n", TRACE_SIZE);
    for (int i = 0; i < TRACE_SIZE; i++) {
        int idx = (m_trace_idx - TRACE_SIZE + i + TRACE_SIZE) % TRACE_SIZE;
        if (m_trace_pc[idx] == 0) {
            printf("  -- skip zero at idx %d (buf was not full yet)\n", idx);
            continue;
        }
        printf("  [%03d] 0x%08X: 0x%08X  insn_count=%llu\n", i, m_trace_pc[idx], m_trace_insn[idx], 0ULL);
    }
}

void CPU::execute_one() {
    if (!running) return;

    u32 insn = fetch();
    u32 fetch_pc = pc;
    trace_add(pc, insn);
    u32 next_pc = pc + 4;
    pc = next_pc;

    // Sync to global for syscall access
    memcpy(g_cpu_regs, regs, sizeof(regs));
    g_cpu_pc = pc;
    g_cpu_hi = hi;
    g_cpu_lo = lo;

    execute(insn);

    // Detect JR/JALR to invalid address immediately
    if ((pc & 0x80000000) == 0 && pc >= 0x4000) {
        printf("[KUSEG] Immediate: pc=0x%08X from insn=0x%08X at 0x%08X\n",
               pc, insn, next_pc - 4);
        printf("[KUSEG] regs[31]=0x%08X regs[29]=0x%08X\n", regs[31], regs[29]);
        running = false;
        return;
    }

    // execute() modifies regs and pc directly; sync regs to global
    memcpy(g_cpu_regs, regs, sizeof(regs));
    g_cpu_hi = hi;
    g_cpu_lo = lo;

    // Handle delay slot
    if (pc != next_pc && pc != 0) {
        u32 branch_target = pc;
        u32 delay_pc = next_pc;

        u32 delay_insn = mem->read_u32(delay_pc);
        pc = delay_pc + 4;

        memcpy(g_cpu_regs, regs, sizeof(regs));
        g_cpu_pc = pc;
        g_cpu_hi = hi;
        g_cpu_lo = lo;
        execute(delay_insn);
        memcpy(g_cpu_regs, regs, sizeof(regs));
        g_cpu_hi = hi;
        g_cpu_lo = lo;

        // Check if delay slot set KUSEG
        if ((pc & 0x80000000) == 0 && pc >= 0x4000) {
            printf("[KUSEG] After delay slot: pc=0x%08X\n", pc);
            printf("[KUSEG] delay_insn=0x%08X branch_target=0x%08X\n",
                   delay_insn, branch_target);
            running = false;
            return;
        }

        pc = branch_target;
    }

    // GOT trampoline check
    if (mem->is_got_address(pc)) {
        int idx = mem->got_index(pc);
        if (idx >= 0 && (u32)idx < GOT_COUNT) {
            u32 return_addr = regs[31];
            syscalls->dispatch(idx, return_addr);
            memcpy(regs, g_cpu_regs, sizeof(regs));
            hi = g_cpu_hi;
            lo = g_cpu_lo;
            pc = regs[31];
            if ((pc & 0x80000000) == 0 && pc >= 0x4000) {
                printf("[KUSEG] GOT dispatch idx=%d pc=0x%08X (invalid)\n", idx, pc);
                printf("[KUSEG] return_addr=0x%08X\n", return_addr);
                running = false;
            }
        }
    }
    insn_count++;
}

void CPU::run_frame(u32 max_insns) {
    for (u32 i = 0; i < max_insns && running; i++) {
        execute_one();
    }
    // Simulate VSYNC once per frame
    if (syscalls) {
        bool switched = syscalls->simulate_vsync();
        memcpy(regs, g_cpu_regs, sizeof(regs));
        hi = g_cpu_hi;
        lo = g_cpu_lo;
        if (switched) {
            pc = regs[31];
        }
    }
}
