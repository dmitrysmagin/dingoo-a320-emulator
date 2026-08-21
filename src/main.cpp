#include "app_parser.h"
#include "memory.h"
#include "cpu.h"
#include "display.h"
#include "archive.h"
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
    u32 arg_max_seconds = 0;  // 0 = unlimited
    bool save_screenshots = false;
    bool nosound = false;
    int audio_latency_ms = Syscalls::AUDIO_TARGET_LATENCY_MS_DEFAULT;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            arg_max_frames = (u32)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
            arg_max_seconds = (u32)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--save-screenshots") == 0) {
            save_screenshots = true;
        } else if (strcmp(argv[i], "--nosound") == 0) {
            nosound = true;
        } else if (strncmp(argv[i], "--audio-latency=", 16) == 0) {
            audio_latency_ms = atoi(argv[i] + 16);
        } else if (strcmp(argv[i], "--audio-latency") == 0 && i + 1 < argc) {
            audio_latency_ms = atoi(argv[++i]);
        } else if (argv[i][0] != '-') {
            app_path = argv[i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            fprintf(stderr, "Usage: %s [--frames <n>] [--seconds <n>] [--save-screenshots] [--nosound] [--audio-latency <ms>] <app>\n", argv[0]);
            return 1;
        }
    }

    if (!app_path) {
        fprintf(stderr, "Usage: %s [--frames <n>] [--seconds <n>] [--save-screenshots] [--nosound] [--audio-latency <ms>] <app>\n", argv[0]);
        return 1;
    }
    printf("=== Dingoo A320 Emulator (Phase 2) ===\n\n");

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

    // Look up AppMain from export table — mandatory; all .app files must export it.
    u32 app_main_addr = 0;
    for (const auto& exp : app.exports) {
        if (exp.name == "AppMain" || exp.name == "app_main") {
            app_main_addr = exp.address;
            printf("[INIT] Found AppMain at 0x%08X from exports\n", app_main_addr);
            break;
        }
    }
    if (!app_main_addr) {
        fprintf(stderr, "[ERROR] No AppMain export found in %s — cannot run\n", app_path);
        return 1;
    }

    // Determine GOT base from imports (minimum trampoline address).
    // IMPORTANT: start from 0 so that ANY import address wins over the fallback.
    u32 got_base = 0;
    for (const auto& imp : app.imports) {
        if (imp.address >= 0x80000000 && (got_base == 0 || imp.address < got_base)) {
            got_base = imp.address;
        }
    }
    if (got_base == 0) {
        got_base = 0x80AD67E0; // genuine fallback when no imports have valid addresses
        printf("[WARN] No valid import addresses found; using default GOT base 0x%08X\n", got_base);
    }
    printf("[INIT] GOT base: 0x%08X (%u entries)\n", got_base, (u32)app.imports.size());
    mem.set_got_range(got_base, (u32)app.imports.size());

    // Write game name as wide string just above the program's BSS, in free RAM.
    // prog_end_vaddr is the first byte past the program image; no game symbols live there.
    u32 name_addr = (prog_end_vaddr + 15u) & ~15u;
    for (size_t i = 0; i < game_name.size(); i++) {
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

    // Zero the stack area.
    // Stack sits near the TOP of 32MB RAM (0x81FF0000, phys 0x01FF0000) so the
    // heap can grow freely upward from BSS without colliding with it.  Placing
    // the stack at 0x80C00000 (12 MB) left only ~1.4 MB between BSS and the
    // stack — games with large framebuffer mallocs (e.g. Block Breaker: 153 KB
    // at 0x80BEA7FE) would overwrite the stack and corrupt $ra with pixel data.
    const u32 STACK_TOP = 0x81FF0000;   // phys 0x01FF0000; 64 KB below 32MB ceiling
    u32 stack_phys = STACK_TOP & 0x1FFFFFFF;
    mem.zero_region(stack_phys - 0x10000, 0x10000);  // zero 64 KB below stack top
    printf("[INIT] Zeroed stack area: phys 0x%08X-0x%08X\n", stack_phys - 0x10000, stack_phys);

    // Pre-populate the event queue exactly as the real Dingoo A320 OS does before launching
    // an app.  Phys 0x00BFECD8 falls inside the resource archive (loaded at 0x00B50000+), so
    // without an explicit write it would contain raw archive data.  Bit 31 of the value signals
    // audio-subsystem ready, causing the game to create its audio task on the first
    // _sys_judge_event call.  The lower bits encode hardware state (earphone jack, etc.) and
    // are used by the game as audio-buffer parameters — 0x8BFC4D89 is the empirically-observed
    // value from real hardware and must be used verbatim.
    mem.write_u32(0x80BFECD8, 0x8BFC4D89u);
    printf("[INIT] Pre-populated event queue 0x80BFECD8 = 0x8BFC4D89 (hardware-ready)\n");

    // Sentinel: dl_main returns to this address, which signals Phase 1 is complete.
    // Sits in the zeroed stack area (0x80BFF000-0x80C10000), so it contains 0x00000000
    // (NOPs) but we never execute there — run_until_pc stops before fetching from it.
    const u32 DL_MAIN_SENTINEL = 0x80BFFE00;

    // Dedicated idle loop for the µC/OS-II scheduler (distinct from DL_MAIN_SENTINEL).
    // OSTimeDly with no other runnable task spins here until vsync unblocks the caller.
    const u32 IDLE_LOOP_PC = 0x80BFFD00;
    mem.write_u32(IDLE_LOOP_PC + 0x00, 0x082FFF40); // j IDLE_LOOP_PC
    mem.write_u32(IDLE_LOOP_PC + 0x04, 0x00000000); // nop
    printf("[PATCH] Scheduler idle loop at 0x%08X\n", IDLE_LOOP_PC);

    // Write timer callback return stub at 0x80BFFF00
    //   lw $ra, 0($sp);  addiu $sp, $sp, 16;  jr $ra;  nop
    // The +16 must match the frame call_guest_function() reserves, otherwise every
    // guest callback leaks stack.
    u32 timer_ret_stub = 0x80BFFF00;
    mem.write_u32(timer_ret_stub + 0x00, 0x8FBF0000);
    mem.write_u32(timer_ret_stub + 0x04, 0x27BD0010);
    mem.write_u32(timer_ret_stub + 0x08, 0x03E00008);
    mem.write_u32(timer_ret_stub + 0x0C, 0x00000000);
    printf("[PATCH] Timer return stub at 0x%08X\n", timer_ret_stub);

    // Initialize display (SDL2)
    Display display;
    if (!display.init()) {
        fprintf(stderr, "Failed to initialize SDL2 display\n");
        return 1;
    }

    // Load SPK resource archive on the host (served via dl_res_* and fsys_fopenW).
    Archive archive;
    Archive* archive_ptr = nullptr;
    if (app.resource_size > 0) {
        if (archive.load(app_path, app.resource_offset, app.resource_size)) {
            archive_ptr = &archive;
            printf("[INIT] Resource archive loaded: %zu entries\n", archive.count());
        } else {
            fprintf(stderr, "[INIT] Warning: failed to parse resource archive from %s\n", app_path);
        }
    } else {
        printf("[INIT] No resource archive present; skipping load\n");
    }

    // Initialize syscalls
    Syscalls syscalls(mem, display);
    syscalls.set_archive(archive_ptr);
    syscalls.set_app_path(app_path);
    syscalls.set_nosound(nosound);
    syscalls.set_audio_target_latency_ms(audio_latency_ms);
    if (audio_latency_ms != Syscalls::AUDIO_TARGET_LATENCY_MS_DEFAULT) {
        printf("[AUDIO] target latency: %d ms (default %d)\n",
               audio_latency_ms, Syscalls::AUDIO_TARGET_LATENCY_MS_DEFAULT);
    }

    // Resolve GOT slot handlers by name (works for all app formats)
    syscalls.init_slot_handlers(app.imports);

    // Initialize CPU
    CPU cpu;
    cpu.mem = &mem;
    cpu.syscalls = &syscalls;
    cpu.reset();

    // Kernel memory addresses for input state (written every frame so guest
    // code that polls these directly — bypassing GOT syscalls — sees SDL input).
    const u32 KERN_KEY_CURRENT  = 0x802DA020; // GPIO scan: current key bitmask
    const u32 KERN_KEY_RELEASED = 0x802DA024; // GPIO scan: keys released this frame
    const u32 KERN_KEY_PRESSED  = 0x802DA028; // GPIO scan: keys pressed this frame
    const u32 KERN_KEY_SCAN_VAL = 0x80242B40; // key_matrix_scan_value
    const u32 KERN_KEY_MAILBOX  = 0x80B39D08; // uC/OS-II keyboard state mailbox

    auto write_keys = [&]() {
        u32 keys = display.get_dingoo_keys();
        u32 hw   = display.get_hw_keys();
        u32 prev = mem.read_u32(KERN_KEY_CURRENT);
        mem.write_u32(KERN_KEY_CURRENT,  keys);
        mem.write_u32(KERN_KEY_RELEASED, ~keys & prev);
        mem.write_u32(KERN_KEY_PRESSED,  keys & ~prev);
        mem.write_u32(KERN_KEY_SCAN_VAL, keys);
        mem.write_u32(KERN_KEY_MAILBOX,  hw);
    };

    printf("[INIT] RAM size: %u MB\n", mem.size() / (1024 * 1024));
    printf("[INIT] Display: %dx%d (scale %d)\n", Display::WIDTH, Display::HEIGHT, Display::SCALE);
    printf("[INIT] Imported APIs:\n");
    for (u32 i = 0; i < (u32)app.imports.size() && i < MAX_GOT_ENTRIES; i++) {
        const char* name = app.imports[i].name.c_str();
        printf("  [%2u] %-30s %s\n", i, name,
               syscalls.got_is_stub((int)i) ? "stub" : "implemented");
    }

    srand((u32)time(NULL));
    clock_t start = clock();
    u32 max_insns_per_frame = 2000000;
    u32 frame_count = 0;

    // --seconds covers the whole run, not just Phase 2: a game whose dl_main never
    // returns would otherwise ignore the limit and hang forever.
    const u32 run_deadline = arg_max_seconds ? SDL_GetTicks() + arg_max_seconds * 1000 : 0;
    if (arg_max_seconds)
        printf("[INIT] Time limit: %u seconds\n", arg_max_seconds);

    // =========================================================
    // Phase 1: run dl_main to completion
    // dl_main is the module initialiser — it allocates resources
    // and sets up state, then returns.  We run it in isolation
    // and detect its return by watching for DL_MAIN_SENTINEL.
    // =========================================================
    cpu.pc = app.entry_point;
    cpu.regs[29] = STACK_TOP;
    cpu.regs[30] = STACK_TOP;
    cpu.regs[31] = DL_MAIN_SENTINEL;
    cpu.regs[4] = 0;   // a0 = argc = 0
    cpu.regs[5] = 0;   // a1 = argv = NULL (first-time init)

    printf("\n=== Phase 1: dl_main at 0x%08X ===\n\n", cpu.pc);
    fflush(stdout);

    {
        u32 dl_frame = 0;
        while (cpu.running && cpu.pc != DL_MAIN_SENTINEL) {
            if (display.pump_events()) { cpu.running = false; break; }
            write_keys();
            cpu.run_until_pc(DL_MAIN_SENTINEL, max_insns_per_frame);
            syscalls.process_timers();
            dl_frame++;
            if (dl_frame % 500 == 0)
                printf("[PHASE 1] frame=%u PC=0x%08X insns=%llu\n", dl_frame, cpu.pc, cpu.insn_count);
            if (run_deadline && SDL_GetTicks() >= run_deadline) {
                printf("[PHASE 1] Time limit reached at frame %u PC=0x%08X — dl_main never returned\n",
                       dl_frame, cpu.pc);
                fflush(stdout);
                return 0;
            }
        }
        printf("[PHASE 1] dl_main returned after %u frames (insns=%llu)\n", dl_frame, cpu.insn_count);
    }

    if (!cpu.running) {
        fprintf(stderr, "[PHASE 1] dl_main did not return cleanly — halting\n");
        cpu.print_trace();
        return 1;
    }

    // Save post-init CPU state as idle context for OSTaskDel fallback.
    // Idle PC is the scheduler spin loop, NOT the dl_main sentinel — using the sentinel
    // here caused OSTimeDly to look like AppMain returning when no peer task exists.
    u32 idle_regs[32] = {};
    idle_regs[29] = STACK_TOP;
    idle_regs[30] = STACK_TOP;
    syscalls.set_idle_regs(idle_regs);
    syscalls.set_idle_pc(IDLE_LOOP_PC);

    // =========================================================
    // Phase 2: call AppMain
    // The OS looks up "AppMain" from the export table after
    // dl_main has returned and calls it with the game name.
    // =========================================================
    cpu.pc        = app_main_addr;
    cpu.regs[31]  = DL_MAIN_SENTINEL; // AppMain is not expected to return; sentinel catches it
    cpu.regs[4]   = name_addr;         // a0 = wide-string game name (already written above)
    // $sp / $fp left as dl_main balanced its own frame

    // Register AppMain as a µC/OS-II task at higher priority than audio (16).
    // AppMain prio 5 means OSTaskCreate(audio, 16) does NOT preempt AppMain —
    // audio only gets CPU when AppMain blocks (OSSemPend/OSTimeDly).
    syscalls.register_main_context(app_main_addr, name_addr, 5);

    printf("\n=== Phase 2: AppMain at 0x%08X ===\n\n", app_main_addr);
    fflush(stdout);

    u32 frame = 0;
    u32 max_frames = arg_max_frames;  // 0 = unlimited
    if (max_frames)
        printf("[INIT] Frame limit: %u CPU frames\n", max_frames);

    while (cpu.running && (max_frames == 0 || frame < max_frames)) {
        if (run_deadline && SDL_GetTicks() >= run_deadline) {
            printf("[HALT] Time limit reached at frame %u\n", frame);
            break;
        }

        // Process SDL events (quit, keyboard)
        if (display.pump_events()) {
            printf("[DISPLAY] Quit requested\n");
            break;
        }

        write_keys();
        cpu.run_until_pc(DL_MAIN_SENTINEL, max_insns_per_frame, IDLE_LOOP_PC);
        syscalls.process_timers();
        cpu.do_vsync();
        frame++;

        // AppMain returned to the sentinel for real (jr $ra where $ra == sentinel).
        // If tasks are still blocked on OSTimeDly/OSSemPend, keep running — that is
        // a scheduler yield, not an exit (should not happen once idle loop is separate).
        if (cpu.pc == DL_MAIN_SENTINEL) {
            if (!cpu.running) break;
            if (syscalls.has_blocked_tasks()) {
                continue;
            }
            printf("[PHASE 2] Sentinel hit at frame %u — no runnable task, stopping\n", frame);
            break;
        }

        // OSTimeDly yielded with no peer task — vsync above may have woken the caller.
        if (cpu.pc == IDLE_LOOP_PC || syscalls.in_idle()) {
            continue;
        }

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
