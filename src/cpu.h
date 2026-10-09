#ifndef CPU_H
#define CPU_H

#include "types.h"
#include "memory.h"
#include "cop0.h"
#include "mxu.h"
#include "syscalls.h"

// Executable-code generation: bumped whenever guest code mappings change
// (dl_load/dl_free/dl_res map+close). The JIT drops its whole TB cache on
// mismatch. Defined in cpu.cpp, mutated in syscalls.cpp, read in jit.cpp.
extern u32 g_code_gen;

struct CPU {
    u32 regs[32];   // GPRs
    u32 pc;         // Program counter
    u32 hi, lo;     // HI/LO for multiply/divide
    u32 llbit;      // Load-linked bit
    u32 ll_addr;    // Load-linked address (for SC match check)

    COP0 cop0;
    MXU  mxu;

    Memory* mem;
    Syscalls* syscalls;

    bool running;
    bool nullify_delay;  // set by likely-branch when not taken; skips delay slot
    u64  insn_count;

    void reset();
    void execute_one();
    // JIT dispatcher fallback: no trace ring, lazy g_cpu sync until GOT dispatch.
    void execute_one_jit();
    void run_until_pc(u32 stop_pc, u32 max_insns, u32 alt_stop_pc = 0);
    void run_frame(u32 max_insns);
    void do_vsync();       // quantum epilogue: sync GPRs, service_os_quantum (no video)
    void print_trace();

private:
    static const int TRACE_SIZE = 256;
    u32 m_trace_pc[TRACE_SIZE];
    u32 m_trace_insn[TRACE_SIZE];
    int m_trace_idx;
    void trace_add(u32 pc, u32 insn);
    void execute_one_impl(bool jit_path);
    u32 fetch();
    void execute(u32 insn);

    // SPECIAL (opcode 0x00)
    void exec_special(u32 insn);
    // REGIMM (opcode 0x01)
    void exec_regimm(u32 insn);
    // SPECIAL2 (opcode 0x1C)
    void exec_special2(u32 insn);
    // SPECIAL3 (opcode 0x1F)
    void exec_special3(u32 insn);

    // Branch helpers
    bool branch_taken(u32 insn, u32& target, bool& has_delay);

    // Load/store helpers
    void load(u32 insn, int size, bool sign_ext, bool left, bool right);
    void store(u32 insn, int size, bool left, bool right);

    // Exception
    void raise_exception(u32 code);
};

#endif // CPU_H
