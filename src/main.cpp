#include "app_parser.h"
#include "memory.h"
#include "cpu.h"
#include "display.h"
#include "syscalls.h"
#undef main
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <algorithm>

int main(int argc, char* argv[]) {
    const char* app_path = nullptr;
    u32 arg_max_frames = 0;  // 0 = unlimited
    bool save_screenshots = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            arg_max_frames = (u32)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--save-screenshots") == 0) {
            save_screenshots = true;
        } else if (argv[i][0] != '-') {
            app_path = argv[i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            fprintf(stderr, "Usage: %s [--frames <n>] [--save-screenshots] <7days.app>\n", argv[0]);
            return 1;
        }
    }

    if (!app_path) {
        fprintf(stderr, "Usage: %s [--frames <n>] [--save-screenshots] <7days.app>\n", argv[0]);
        return 1;
    }
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

    // Extract game name from app_path (basename without extension)
    std::string app_path_str = app_path;
    std::string game_name;
    {
        size_t slash = app_path_str.find_last_of("/\\");
        std::string basename = (slash != std::string::npos) ? app_path_str.substr(slash + 1) : app_path_str;
        size_t dot = basename.find_last_of('.');
        game_name = (dot != std::string::npos) ? basename.substr(0, dot) : basename;
        printf("[INIT] Game name: '%s'\n", game_name.c_str());
    }

    // Look up AppMain from export table
    u32 app_main_addr = 0x80AD6B1C; // fallback to known 7days value
    for (const auto& exp : app.exports) {
        if (exp.name == "AppMain" || exp.name == "app_main") {
            app_main_addr = exp.address;
            printf("[INIT] Found AppMain at 0x%08X from exports\n", app_main_addr);
            break;
        }
    }

    // Determine GOT base from imports (minimum trampoline address)
    u32 got_base = 0x80AD67E0; // fallback to 7days default
    for (const auto& imp : app.imports) {
        if (imp.address >= 0x80000000 && (got_base == 0 || imp.address < got_base)) {
            got_base = imp.address;
        }
    }
    printf("[INIT] GOT base: 0x%08X (%u entries)\n", got_base, (u32)app.imports.size());
    mem.set_got_range(got_base, (u32)app.imports.size());

    // Patch: fix SLTI bug at 0x80ADE0DC - compares $zero instead of $s0,
    // causing the event loop to never exit. Change 0x2A0200B0 to 0x2A1000B0.
    /*{
        u32 patch_vaddr = 0x80ADE0DC;
        u32 current = mem.read_u32(patch_vaddr);
        if (current == 0x2A0200B0) {
            mem.write_u32(patch_vaddr, 0x2A1000B0);
            printf("[PATCH] Fixed SLTI at 0x%08X: 0x%08X -> 0x2A1000B0\n", patch_vaddr, current);
        } else {
            printf("[PATCH] SLTI at 0x%08X = 0x%08X (not 7days, not patching)\n", patch_vaddr, current);
        }
    }*/

    // Write game name as wide string at 0x80B44FE0 (for AppMain/game_main argument)
    u32 name_addr = 0x80B44FE0;
    for (size_t i = 0; i < game_name.size() && i < 32; i++) {
        mem.write_u16(name_addr + (u32)i * 2, (u16)(unsigned char)game_name[i]);
    }
    mem.write_u16(name_addr + (u32)game_name.size() * 2, 0); // null terminator

    // Note: code section protection was intentionally REMOVED.
    // The game's idle/task stacks are in the RAWD/BSS boundary area (see KUSEG bug history).
    // Real Dingoo A320 has no read-only code protection, so neither should we.
    // (set_code_region not called = no protection)

    // Resource archive is NOT loaded into guest RAM: the real Dingoo A320 has only 32 MB DRAM
    // and resources are streamed from NAND storage via filesystem APIs, not memory-mapped.
    // The Archive class (host-side) serves all fsys_fopen/fsys_fread calls independently.

    // Zero the stack area
    // Stack starts at 0x80C00000, use 64KB window
    u32 stack_phys = 0x80C00000 & 0x1FFFFFFF;
    mem.zero_region(stack_phys - 0x1000, 0x11000);
    printf("[INIT] Zeroed stack area: phys 0x%08X-0x%08X\n", stack_phys - 0x1000, stack_phys + 0x10000);

    // Pre-populate the event queue exactly as the real Dingoo A320 OS does before launching
    // an app.  Phys 0x00BFECD8 falls inside the resource archive (loaded at 0x00B50000+), so
    // without an explicit write it would contain raw archive data.  Bit 31 of the value signals
    // audio-subsystem ready, causing the game to create its audio task on the first
    // _sys_judge_event call.  The lower bits encode hardware state (earphone jack, etc.) and
    // are used by the game as audio-buffer parameters — 0x8BFC4D89 is the empirically-observed
    // value from real hardware and must be used verbatim.
    mem.write_u32(0x80BFECD8, 0x8BFC4D89u);
    printf("[INIT] Pre-populated event queue 0x80BFECD8 = 0x8BFC4D89 (hardware-ready)\n");

    // Write BSS stub at 0x80BFF000 (in stack area, AFTER stack zero)
    // The stub writes a marker, then jumps to AppMain with a0 = game name addr.
    // Game name was already written at 0x80B44FE0 above, so stub just sets a0 & jumps.
    u32 stub_addr = 0x80BFF000;
    u32 j_insn = 0x08000000 | ((app_main_addr >> 2) & 0x03FFFFFF);
    // LUI t0, 0x80B4 ; LUI t1, 0xCAFE ; ORI t1, t1, 0xBABE ; SW t1, 0x3F00(t0) = *(0x80B43F00) = 0xCAFEBABE
    mem.write_u32(stub_addr + 0x00, 0x3C0880B4);  // LUI t0, 0x80B4
    mem.write_u32(stub_addr + 0x04, 0x3C09CAFE);  // LUI t1, 0xCAFE
    mem.write_u32(stub_addr + 0x08, 0x3529BABE);  // ORI t1, t1, 0xBABE
    mem.write_u32(stub_addr + 0x0C, 0xAD093F00);  // SW  t1, 0x3F00(t0)
    // Set a0 = 0x80B44FE0 and jump to AppMain
    mem.write_u32(stub_addr + 0x10, 0x3C0480B4);  // LUI a0, 0x80B4
    mem.write_u32(stub_addr + 0x14, j_insn);       // J   app_main_addr
    mem.write_u32(stub_addr + 0x18, 0x34844FE0);  // ORI a0, a0, 0x4FE0 (delay slot)
    printf("[PATCH] BSS stub at 0x%08X -> AppMain 0x%08X\n", stub_addr, app_main_addr);

    // Write timer callback return stub at 0x80BFFF00
    //   lw $ra, 0($sp);  addiu $sp, $sp, 8;  jr $ra;  nop
    u32 timer_ret_stub = 0x80BFFF00;
    mem.write_u32(timer_ret_stub + 0x00, 0x8FBF0000);
    mem.write_u32(timer_ret_stub + 0x04, 0x27BD0008);
    mem.write_u32(timer_ret_stub + 0x08, 0x03E00008);
    mem.write_u32(timer_ret_stub + 0x0C, 0x00000000);
    printf("[PATCH] Timer return stub at 0x%08X\n", timer_ret_stub);

    // Initialize display (SDL2)
    Display display;
    if (!display.init()) {
        fprintf(stderr, "Failed to initialize SDL2 display\n");
        return 1;
    }

    // Load resource archive
    Archive archive;
    if (app.resource_size > 0) {
        if (!archive.load(app_path, app.resource_offset, app.resource_size)) {
            fprintf(stderr, "Failed to load resource archive from %s\n", app_path);
            return 1;
        }
        printf("[INIT] Resource archive loaded: %zu entries\n", archive.count());
    } else {
        printf("[INIT] No resource archive present; skipping load\n");
    }

    // Initialize syscalls
    Syscalls syscalls(mem, display);
    syscalls.set_archive(&archive);
    syscalls.set_app_path(app_path);

    // Initialize CPU
    CPU cpu;
    cpu.mem = &mem;
    cpu.syscalls = &syscalls;
    syscalls.set_cop0(&cpu.cop0);
    cpu.reset();

    // Set up initial state for dl_main
    cpu.pc = app.entry_point;
    cpu.regs[29] = 0x80C00000;
    cpu.regs[30] = 0x80C00000;
    cpu.regs[31] = stub_addr;  // return to stub after init
    cpu.regs[4] = 0;   // a0 = argc = 0
    cpu.regs[5] = 0;   // a1 = argv = 0 (0 = first-time init)

    printf("[INIT] Entry point: 0x%08X (dl_main)\n", cpu.pc);
    printf("[INIT] Stack: 0x%08X\n", cpu.regs[29]);
    printf("[INIT] RAM size: %u MB\n", mem.size() / (1024 * 1024));
    printf("[INIT] Display: %dx%d (scale %d)\n", Display::WIDTH, Display::HEIGHT, Display::SCALE);
    printf("[INIT] Imported APIs:\n");
    for (u32 i = 0; i < (u32)app.imports.size() && i < MAX_GOT_ENTRIES; i++) {
        const char* impl = syscalls.got_is_stub((int)i) ? "stub" : "implemented";
        printf("  [%2u] %-30s %s\n", i, syscalls.got_name((int)i), impl);
    }

    // Save initial CPU state as idle context for OSTaskDel fallback
    extern u32 g_cpu_regs[32];
    memcpy(g_cpu_regs, cpu.regs, sizeof(g_cpu_regs));
    syscalls.set_idle_regs(g_cpu_regs);
    syscalls.set_idle_pc(cpu.pc);

    printf("\n=== Starting emulation ===\n\n");
    fflush(stdout);

    srand((u32)time(NULL));

    clock_t start = clock();
    u32 frame = 0;
    u32 max_insns_per_frame = 2000000;
    u32 max_frames = arg_max_frames;  // 0 = unlimited (runs until quit or CPU halts)
    u32 frame_count = 0;

    if (max_frames)
        printf("[INIT] Frame limit: %u CPU frames\n", max_frames);
    // Kernel memory addresses for input state (written every frame so guest
    // code that polls these directly — bypassing GOT syscalls — sees SDL input).
    const u32 KERN_KEY_CURRENT  = 0x802DA020; // GPIO scan: current key bitmask
    const u32 KERN_KEY_RELEASED = 0x802DA024; // GPIO scan: keys released this frame
    const u32 KERN_KEY_PRESSED  = 0x802DA028; // GPIO scan: keys pressed this frame
    const u32 KERN_KEY_SCAN_VAL = 0x80242B40; // key_matrix_scan_value
    const u32 KERN_KEY_MAILBOX  = 0x80B39D08; // uC/OS-II keyboard state mailbox

    while (cpu.running && (max_frames == 0 || frame < max_frames)) {
        // Process SDL events (quit, keyboard)
        if (display.pump_events()) {
            printf("[DISPLAY] Quit requested\n");
            break;
        }

        // Write current key state to all kernel memory locations the game might poll
        {
            u32 keys = display.get_dingoo_keys();
            u32 prev = mem.read_u32(KERN_KEY_CURRENT);
            u32 pressed  = keys & ~prev;
            u32 released = ~keys & prev;
            mem.write_u32(KERN_KEY_CURRENT,  keys);
            mem.write_u32(KERN_KEY_RELEASED, released);
            mem.write_u32(KERN_KEY_PRESSED,  pressed);
            mem.write_u32(KERN_KEY_SCAN_VAL, keys);
            mem.write_u32(KERN_KEY_MAILBOX,  keys);
        }

        cpu.run_frame(max_insns_per_frame);
        syscalls.process_timers();
        frame++;

        // Check if PC is in valid code region
        u32 pc_phys = cpu.pc & 0x1FFFFFFF;
        if (pc_phys >= mem.size()) {
            printf("[HALT] PC=0x%08X outside RAM (phys=0x%08X)\n", cpu.pc, pc_phys);
            cpu.running = false;
        }

        // Present frame if display is dirty; optionally save screenshots
        if (display.is_dirty()) {
            display.clear_dirty();
            frame_count++;
            if (save_screenshots && (frame_count <= 10 || frame_count % 10 == 0)) {
                char path[64];
                snprintf(path, sizeof(path), "screenshot_%04d.bmp", frame_count);
                display.save_screenshot(path);
            }
        }

        if (frame % 1000 == 0) {
            clock_t elapsed = clock() - start;
            double seconds = (double)elapsed / CLOCKS_PER_SEC;
            double insns_per_sec = cpu.insn_count / (seconds > 0 ? seconds : 0.001);
            printf("[FRAME %u] PC=0x%08X insns=%llu (%.0f/s) rendered=%u got=%u\n",
                   frame, cpu.pc, cpu.insn_count, insns_per_sec, frame_count,
                   syscalls.got_call_count());
            int key_gots[] = {55, 60, 61, 63, 65, 66, 67, 49};
            for (int gi = 0; gi < 8; gi++) {
                int g = key_gots[gi];
                printf("  GOT[%2d] %-20s %u\n", g, syscalls.got_name(g), syscalls.got_call_counts(g));
            }
            fflush(stdout);
        }

        if (!cpu.running) {
            printf("\n[EMULATION STOPPED] PC=0x%08X total_insns=%llu frames=%u\n",
                   cpu.pc, cpu.insn_count, frame);
        }
    }
    fflush(stdout);

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
    for (int i = 0; i < (int)app.imports.size(); i++) {
        if (syscalls.got_call_counts(i) > 0) {
            const char* impl = syscalls.got_is_stub(i) ? "(stub)" : "";
            printf("  [%2d] %-30s %u %s\n", i, syscalls.got_name(i), syscalls.got_call_counts(i), impl);
        }
    }

    cpu.print_trace();

    printf("\nRegisters:\n");
    for (int i = 0; i < 32; i += 4) {
        printf("  $%2d: %08X  $%2d: %08X  $%2d: %08X  $%2d: %08X\n",
               i, cpu.regs[i], i+1, cpu.regs[i+1], i+2, cpu.regs[i+2], i+3, cpu.regs[i+3]);
    }
    printf("  HI: %08X  LO: %08X\n", cpu.hi, cpu.lo);

    fflush(stdout);
    syscalls.shutdown_audio();
    display.shutdown();
    return 0;
}
