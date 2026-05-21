#include "app_parser.h"
#include "memory.h"
#include "cpu.h"
#include "display.h"
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
    printf("=== 7days Dingoo A320 Emulator (Phase 2) ===\n\n");

    // Parse the .app file
    AppBinary app;
    if (!parse_app(app_path, app)) {
        fprintf(stderr, "Failed to parse %s\n", app_path);
        return 1;
    }

    // Initialize memory
    Memory mem;
    u32 rawd_phys = app.load_addr & 0x1FFFFFFF;
    if (!mem.load_raw(app.raw_data, rawd_phys)) {
        fprintf(stderr, "Failed to load RAWD\n");
        return 1;
    }

    // Zero BSS
    u32 bss_start_phys = BSS_START_ADDR & 0x1FFFFFFF;
    u32 bss_size = BSS_END_ADDR - BSS_START_ADDR;
    printf("[INIT] Zeroing BSS: 0x%08X-0x%08X (%u bytes)\n", BSS_START_ADDR, BSS_END_ADDR, bss_size);
    mem.zero_region(bss_start_phys, bss_size);

    // Initialize display (SDL2)
    Display display;
    if (!display.init()) {
        fprintf(stderr, "Failed to initialize SDL2 display\n");
        return 1;
    }

    // Initialize syscalls
    Syscalls syscalls(mem, display);

    // Initialize CPU
    CPU cpu;
    cpu.mem = &mem;
    cpu.syscalls = &syscalls;
    cpu.reset();

    // Set up initial state for dl_main
    cpu.pc = app.entry_point;
    cpu.regs[29] = 0x80C00000;
    cpu.regs[30] = 0x80C00000;
    cpu.regs[4] = 0;
    cpu.regs[5] = 0;

    printf("[INIT] Entry point: 0x%08X (dl_main)\n", cpu.pc);
    printf("[INIT] Stack: 0x%08X\n", cpu.regs[29]);
    printf("[INIT] RAM size: %u MB\n", mem.size() / (1024 * 1024));
    printf("[INIT] Display: %dx%d (scale %d)\n", Display::WIDTH, Display::HEIGHT, Display::SCALE);

    printf("\n=== Starting emulation ===\n\n");

    srand((u32)time(NULL));

    clock_t start = clock();
    u32 frame = 0;
    u32 max_insns_per_frame = 100000;
    u32 max_frames = 100000;
    u32 frame_count = 0;

    while (cpu.running && frame < max_frames) {
        // Process SDL events (quit, keyboard)
        if (display.pump_events()) {
            printf("[DISPLAY] Quit requested\n");
            break;
        }

        cpu.run_frame(max_insns_per_frame);
        frame++;

        // Check if PC is in valid code region
        u32 pc_phys = cpu.pc & 0x1FFFFFFF;
        if (pc_phys >= mem.size()) {
            printf("[HALT] PC=0x%08X outside RAM (phys=0x%08X)\n", cpu.pc, pc_phys);
            cpu.running = false;
        }

        // Present frame if display is dirty
        if (display.is_dirty()) {
            display.clear_dirty();
            frame_count++;
        }

        if (frame % 1000 == 0) {
            clock_t elapsed = clock() - start;
            double seconds = (double)elapsed / CLOCKS_PER_SEC;
            double insns_per_sec = cpu.insn_count / (seconds > 0 ? seconds : 0.001);
            printf("[FRAME %u] PC=0x%08X insns=%llu (%.0f/s) rendered=%u\n",
                   frame, cpu.pc, cpu.insn_count, insns_per_sec, frame_count);
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
    printf("Frames rendered: %u\n", frame_count);
    printf("Total time: %.3f seconds\n", (double)total / CLOCKS_PER_SEC);
    if (total > 0) {
        printf("Instructions/second: %.0f\n", cpu.insn_count / ((double)total / CLOCKS_PER_SEC));
    }
    printf("Final PC: 0x%08X\n", cpu.pc);

    cpu.print_trace();

    printf("\nRegisters:\n");
    for (int i = 0; i < 32; i += 4) {
        printf("  $%2d: %08X  $%2d: %08X  $%2d: %08X  $%2d: %08X\n",
               i, cpu.regs[i], i+1, cpu.regs[i+1], i+2, cpu.regs[i+2], i+3, cpu.regs[i+3]);
    }
    printf("  HI: %08X  LO: %08X\n", cpu.hi, cpu.lo);

    display.shutdown();
    return 0;
}
