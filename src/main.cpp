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

    // Zero BSS (from end of RAWD to end of program)
    u32 rawd_end_vaddr = app.load_addr + (u32)app.raw_data.size();
    u32 prog_end_vaddr = app.load_addr + app.prog_size;
    u32 bss_start_phys = rawd_end_vaddr & 0x1FFFFFFF;
    u32 bss_size = prog_end_vaddr - rawd_end_vaddr;
    printf("[INIT] Zeroing BSS: 0x%08X-0x%08X (%u bytes)\n", rawd_end_vaddr, prog_end_vaddr, bss_size);
    mem.zero_region(bss_start_phys, bss_size);

    // Patch: fix SLTI bug at 0x80ADE0DC - compares $zero instead of $s0,
    // causing the event loop to never exit. Change 0x2A0200B0 to 0x2A1000B0.
    u32 patch_vaddr = 0x80ADE0DC;
    u32 current = mem.read_u32(patch_vaddr);
    if (current == 0x2A0200B0) {
        mem.write_u32(patch_vaddr, 0x2A1000B0);
        printf("[PATCH] Fixed SLTI at 0x%08X: 0x%08X -> 0x2A1000B0\n", patch_vaddr, current);
    } else {
        printf("[PATCH] SLTI at 0x%08X = 0x%08X (unexpected, not patching)\n", patch_vaddr, current);
    }

    // Write a trampoline that dl_main returns to via JR $ra.
    // dl_main(0,0) does SDK init, then JR $ra to this trampoline.
    // The trampoline sets a0=0 (argc) and jumps to AppMain (0x80AD6B1C),
    // which correctly initializes the game.
    // Location: 0x80B44FF0 (after BSS end, before heap start)
    u32 trampoline_addr = 0x80B44FF0;
    mem.write_u32(trampoline_addr,     0x20040000);  // ADDIU a0, zero, 0
    mem.write_u32(trampoline_addr + 4, 0x082B5AC7);  // J 0x80AD6B1C (AppMain)
    printf("[PATCH] Return trampoline at 0x%08X\n", trampoline_addr);

    // Game has a built-in callback at 0x80A0001C that already calls _lcd_set_frame (GOT 17).
    // No additional patching needed. The callback is in the binary itself.

    // Note: code section protection was intentionally REMOVED.
    // The game's idle/task stacks are in the RAWD/BSS boundary area (see KUSEG bug history).
    // Real Dingoo A320 has no read-only code protection, so neither should we.
    // (set_code_region not called = no protection)

    // Load resource archive into guest memory
    // The Dingoo loader places resources right after the program, aligned to 64KB:
    // resource_base = (load_addr + prog_size + 0xFFFF) & ~0xFFFF
    u32 resource_base = (app.load_addr + app.prog_size + 0xFFFF) & ~0xFFFF;
    u32 resource_phys = resource_base & 0x1FFFFFFF;
    printf("[INIT] Resource archive base: 0x%08X (phys 0x%08X)\n", resource_base, resource_phys);
    if (!mem.load_from_file(app_path, RESOURCE_OFFSET, resource_phys, RESOURCE_SIZE)) {
        fprintf(stderr, "Failed to load resource archive\n");
        return 1;
    }
    printf("[INIT] Resource archive loaded: %u bytes at 0x%08X\n", RESOURCE_SIZE, resource_phys);

    // Zero the stack area (was overwritten by resource archive)
    // Stack starts at 0x80C00000, use 64KB window
    u32 stack_phys = 0x80C00000 & 0x1FFFFFFF;
    mem.zero_region(stack_phys - 0x1000, 0x11000);
    printf("[INIT] Zeroed stack area: phys 0x%08X-0x%08X\n", stack_phys - 0x1000, stack_phys + 0x10000);

    // Initialize display (SDL2)
    Display display;
    if (!display.init()) {
        fprintf(stderr, "Failed to initialize SDL2 display\n");
        return 1;
    }

    // Load resource archive
    Archive archive;
    if (!archive.load(app_path)) {
        fprintf(stderr, "Failed to load resource archive from %s\n", app_path);
        return 1;
    }
    printf("[INIT] Resource archive loaded: %zu entries\n", archive.count());

    // Initialize syscalls
    Syscalls syscalls(mem, display);
    syscalls.set_archive(&archive);

    // Initialize CPU
    CPU cpu;
    cpu.mem = &mem;
    cpu.syscalls = &syscalls;
    cpu.reset();

    // Set up initial state for dl_main
    cpu.pc = app.entry_point;
    cpu.regs[29] = 0x80C00000;
    cpu.regs[30] = 0x80C00000;
    cpu.regs[31] = trampoline_addr;  // return to trampoline after init
    cpu.regs[4] = 0;   // a0 = argc = 0
    cpu.regs[5] = 0;   // a1 = argv = 0 (0 = first-time init)

    printf("[INIT] Entry point: 0x%08X (dl_main)\n", cpu.pc);
    printf("[INIT] Stack: 0x%08X\n", cpu.regs[29]);
    printf("[INIT] RAM size: %u MB\n", mem.size() / (1024 * 1024));
    printf("[INIT] Display: %dx%d (scale %d)\n", Display::WIDTH, Display::HEIGHT, Display::SCALE);


    printf("\n=== Starting emulation ===\n\n");

    srand((u32)time(NULL));

    clock_t start = clock();
    u32 frame = 0;
    u32 max_insns_per_frame = 100000;
    u32 max_frames = 1600;
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

        if (frame > 0 && frame % 1000 == 0) {
            clock_t elapsed = clock() - start;
            double seconds = (double)elapsed / CLOCKS_PER_SEC;
            double insns_per_sec = cpu.insn_count / (seconds > 0 ? seconds : 0.001);
            printf("[FRAME %u] PC=0x%08X insns=%llu (%.0f/s) rendered=%u got=%u\n",
                   frame, cpu.pc, cpu.insn_count, insns_per_sec, frame_count, syscalls.got_call_count());
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

    printf("\nGOT call counts:\n");
    for (int i = 0; i < 72; i++) {
        if (syscalls.got_call_counts(i) > 0)
            printf("  [%2d] %-30s %u\n", i, syscalls.got_name(i), syscalls.got_call_counts(i));
    }

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
