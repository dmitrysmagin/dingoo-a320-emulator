#include "app_parser.h"
#include "memory.h"
#include "cpu.h"
#include "syscalls.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <7days.app>\n", argv[0]);
        return 1;
    }

    const char* app_path = argv[1];
    printf("=== 7days Dingoo A320 Emulator (Phase 1) ===\n\n");

    // Parse the .app file
    AppBinary app;
    if (!parse_app(app_path, app)) {
        fprintf(stderr, "Failed to parse %s\n", app_path);
        return 1;
    }

    printf("\n[INIT] Imports: %u\n", (u32)app.imports.size());
    for (size_t i = 0; i < app.imports.size(); i++) {
        printf("  [%02u] 0x%08X %s\n", (u32)i, app.imports[i].address, app.imports[i].name.c_str());
    }

    printf("\n[INIT] Exports: %u\n", (u32)app.exports.size());
    for (size_t i = 0; i < app.exports.size(); i++) {
        printf("  [%02u] 0x%08X %s\n", (u32)i, app.exports[i].address, app.exports[i].name.c_str());
    }

    // Initialize memory
    Memory mem;
    u32 rawd_phys = app.load_addr & 0x1FFFFFFF;  // strip KSEG bits
    if (!mem.load_raw(app.raw_data, rawd_phys)) {
        fprintf(stderr, "Failed to load RAWD\n");
        return 1;
    }

    // Zero BSS
    u32 bss_start_phys = BSS_START_ADDR & 0x1FFFFFFF;
    u32 bss_size = BSS_END_ADDR - BSS_START_ADDR;
    printf("[INIT] Zeroing BSS: 0x%08X-0x%08X (%u bytes)\n", BSS_START_ADDR, BSS_END_ADDR, bss_size);
    mem.zero_region(bss_start_phys, bss_size);

    // Initialize syscalls
    Syscalls syscalls(mem);

    // Initialize CPU
    CPU cpu;
    cpu.mem = &mem;
    cpu.syscalls = &syscalls;
    cpu.reset();

    // Set up initial state for dl_main
    cpu.pc = app.entry_point;  // dl_main
    cpu.regs[29] = 0x80C00000;  // $sp = stack top
    cpu.regs[30] = 0x80C00000;  // $fp = stack top
    cpu.regs[4] = 0;  // $a0 = 0 (init mode)
    cpu.regs[5] = 0;  // $a1 = 0 (cold boot)

    printf("\n[INIT] Entry point: 0x%08X (dl_main)\n", cpu.pc);
    printf("[INIT] Stack: 0x%08X\n", cpu.regs[29]);
    printf("[INIT] RAM size: %u MB\n", mem.size() / (1024 * 1024));

    printf("\n=== Starting emulation ===\n\n");

    // Seed random
    srand((u32)time(NULL));

    // Run emulation in frames
    clock_t start = clock();
    u32 frame = 0;
    u32 max_insns_per_frame = 100000;
    u32 max_frames = 10000;

    while (cpu.running && frame < max_frames) {
        cpu.run_frame(max_insns_per_frame);
        frame++;

        // Check if PC is in valid code region
        u32 pc_phys = cpu.pc & 0x1FFFFFFF;
        if (pc_phys >= mem.size()) {
            printf("[HALT] PC=0x%08X outside RAM (phys=0x%08X)\n", cpu.pc, pc_phys);
            cpu.running = false;
        }

        if (frame % 100 == 0) {
            clock_t elapsed = clock() - start;
            double seconds = (double)elapsed / CLOCKS_PER_SEC;
            double insns_per_sec = cpu.insn_count / (seconds > 0 ? seconds : 0.001);
            printf("[FRAME %u] PC=0x%08X insns=%llu (%.0f insns/sec)\n",
                   frame, cpu.pc, cpu.insn_count, insns_per_sec);
        }

        if (!cpu.running) {
            printf("\n[EMULATION STOPPED] PC=0x%08X total_insns=%llu frames=%u\n",
                   cpu.pc, cpu.insn_count, frame);
        }
    }

    clock_t total = clock() - start;
    printf("\n=== Emulation Summary ===\n");
    printf("Total frames: %u\n", frame);
    printf("Total instructions: %llu\n", cpu.insn_count);
    printf("Total syscalls dispatched: %u\n", syscalls.got_call_count());
    printf("Total time: %.3f seconds\n", (double)total / CLOCKS_PER_SEC);
    if (total > 0) {
        printf("Instructions/second: %.0f\n", cpu.insn_count / ((double)total / CLOCKS_PER_SEC));
    }
    printf("Final PC: 0x%08X\n", cpu.pc);

    // Print register dump
    printf("\nRegisters:\n");
    for (int i = 0; i < 32; i += 4) {
        printf("  $%2d: %08X  $%2d: %08X  $%2d: %08X  $%2d: %08X\n",
               i, cpu.regs[i], i+1, cpu.regs[i+1], i+2, cpu.regs[i+2], i+3, cpu.regs[i+3]);
    }
    printf("  HI: %08X  LO: %08X\n", cpu.hi, cpu.lo);

    return 0;
}
