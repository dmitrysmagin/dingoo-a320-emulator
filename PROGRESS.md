# Progress — 7days Dingoo Emulator

## Goal
Boot the Dingoo game **"7days"** (`7days.app`, SPK archive) to playable gameplay in a custom C++17/SDL2 emulator (MinGW, Windows).

## What Works

### Archive & Loading
- SPK archive parsing (`archive.cpp`) — 3,216 resource entries extracted on demand
- RAWD binary loading, BSS zeroing, stack area initialisation
- Resource archive loaded into guest RAM at phys `0x00B50000` (64 KB-aligned after prog_size)
- Game name `"7days"` written as UTF-16LE at `0x80B44FE0` (survives `dl_main` BSS clear via BSS stub)

### CPU Emulation
- MIPS32r1 core, all standard opcodes including LWL/LWR (correct little-endian formulas)
- COP0 stubs (Count/Compare auto-increment per instruction, TLB no-ops)
- COP2 / MXU coprocessor (`mxu.cpp`) — all encountered ops handled; zero unknown-opcode hits across 6.4B+ instructions
- GOT trampoline dispatch for all 72 import entries
- 6.4+ billion instructions executed in a 2-minute run with no crashes

### Display
- 320×240 internal framebuffer, scaled 3× → 960×720 via SDL2
- **Current pipeline** — `_lcd_set_frame(end_ptr)`:
  - Game passes the **end** of the framebuffer (back_buf + 153,600 bytes), not the start
  - `impl__lcd_set_frame` subtracts `FB_SIZE = 153,600` to recover the actual start address
  - Strips KSEG bits (`& 0x1FFFFFFF`) to get physical address
  - Calls `m_display.flip()` which reads RGB565 pixels from guest RAM
- `_lcd_get_frame()` returns the back buffer address as a KSEG1 uncached pointer (`phys | 0xA0000000`)
- `g_detected_fb_addr` intercepted at PC `0x80A21E78` (CPU LW instruction) = phys `0x0011E890`
- Double-buffering confirmed: `_lcd_set_frame` and `_lcd_get_frame` called equal numbers of times
- RGB565 → ARGB8888 conversion with proper 5→8 and 6→8 bit replication (top bits fill LSBs)
- Screenshot auto-save: frames 1–10, then every 10th frame thereafter
- F12 manual screenshot; Escape to quit

### Scheduling & Tasks
- Dual µC/OS-II task scheduling via preemptive time-slicing in `simulate_vsync()`
- Both tasks get CPU time each vsync; same-priority tasks round-robin
- `OSTaskCreate` / `OSSemCreate` / `OSSemPend` / `OSSemPost` all functional

### Input
- Full SDL keyboard → Dingoo bitmask mapping (Z=A, X=B, A=X, S=Y, Q=L, W=R, Enter=START, Tab=SELECT)
- `_kbd_get_status` writes key bitmask to both `$v0` register **and** guest RAM at `0x80B49D08`
  (the second write was the root cause fix for text never advancing)
- Event queue at `0x80BFECD8`; `_sys_judge_event` reads and clears it each call
- Auto-press schedule: START at vsync 100, A every 10 vsyncs from vsync 150 onwards (held 5 vsyncs each)
- `SDL_VIDEODRIVER=offscreen` + software renderer fallback for headless/CI testing

### Audio (partial)
- `waveout_open` / `waveout_write` / `waveout_close` all accepted by audio task
- `waveout_can_write` returns 4,096 (always ready) to keep audio task unblocked
- Audio task runs correctly — `waveout_write` called ~22M times per 9,990-frame test run
- **No actual audio output** — samples are discarded; see Missing section

### Filesystem
- `fsys_fopenW` / `fsys_fread` / `fsys_fseek` / `fsys_ftell` / `fsys_fclose` implemented
- All resource files served from embedded SPK archive (3,216 entries)
- Save files (`slot1-3.sav`, `config.sdt`) return NOT FOUND — game handles gracefully

## Patches Applied

| Fix | Address / File | What |
|-----|---------------|------|
| SLTI bug | `0x80ADE0DC` | `0x2A0200B0` → `0x2A1000B0` — scheduler loop exits on NULL entry |
| BSS name stub | `0x80BFF000` | MIPS stub writes `"7days"` UTF-16LE to BSS and jumps to AppMain |
| Event queue pre-pop | `main.cpp` | Writes `0x8BFC4D89` at `0x80BFECD8` — audio-subsystem-ready signal |
| LWL/LWR | `cpu.cpp` | Fixed little-endian formulas and UB at shift=0 |
| `impl_sprintf` | `syscalls.cpp` | Was no-op; now performs format substitutions |
| Task PC field | `syscalls.cpp` | Dedicated `pc` field in `Task` struct — eliminates `$ra` corruption on preemption |
| Duplicate task priority | `syscalls.cpp` | Changed FAIL → WARNING + allow; enables round-robin scheduling |
| Scheduler lazy start | `syscalls.cpp` | `simulate_vsync()` auto-starts on first call |
| Code section protection | `main.cpp` | Removed — game's idle/task stacks overlap RAWD/BSS boundary |
| Screenshot bit expansion | `display.cpp` | SDL `ConvertSurfaceFormat` replaces manual loop; white → (255,255,255) not (248,252,248) |
| SDL software renderer fallback | `display.cpp` | Allows `SDL_VIDEODRIVER=offscreen` for headless tests |
| `_kbd_get_status` dual-write | `syscalls.cpp` | Also writes keys to guest RAM `0x80B49D08`; without this text never advanced |

## Current Status

### Game Progression (9,990-frame test run)
- Boots through `dl_main` → `GameEngineInit` → `AppMain`
- **Flip #1**: Title/intro CG — 75,419 non-black pixels (phys `0x0011E890`), nearly full 320×240 screen
- **Flip #2–3**: 0 non-black pixels (framebuffers cleared between scenes)
- **Flip #4 onwards**: 1,269 non-black pixels per frame — Chinese text strip only (x=128–314, y=222–239)
  - Background is permanently black throughout the entire prologue
  - Text IS advancing — all 9,990 frames have distinct content (confirmed by frame hash comparison)
  - Pixel count and position remain constant; only glyph content changes
- No crashes, no unknown opcodes across the entire run

### Open Problem: Prologue Backgrounds Never Appear
Three large resource reads occur at startup (before/during prologue):
- 77,240 bytes at file offset ~`0x02EB63FC` (prologue background 1)
- 77,324 bytes (prologue background 2)
- 90,888 bytes (prologue background 3)

These are far smaller than an uncompressed RGB565 framebuffer (153,600 bytes), confirming they are
compressed images. The game must decompress them before writing to the framebuffer.

**Hypothesis**: The JZ4740 SoC includes an **Image Processing Unit (IPU)** — a hardware 2D blitter
capable of DMA-based image decoding, scaling, and blitting. The Dingoo A320 firmware uses this for
CG background rendering. Because the emulator does not implement IPU MMIO registers, the decompressed
pixel data never reaches guest RAM and the framebuffer remains black.

**Supporting evidence**:
- `GOT[20] ap_lcd_set_frame` and `GOT[21] lcd_flip` are never called — the background blitting
  does not go through those paths
- The title CG (431,444 bytes — a full uncompressed 320×240 frame) renders correctly via the normal
  `_lcd_set_frame` path; only compressed prologue images fail

### Performance
- ~64M guest instructions/second on modern x86
- 2-minute run: ~9,990 rendered frames, ~52,000 CPU frames at 2M insns/frame
- Audio task: ~22M `waveout_write` calls, ~22M each `OSSemPend`/`OSSemPost` — scheduler healthy

### Run Command
```
cd emulator
SDL_VIDEODRIVER=offscreen timeout 60 ./emulator.exe ../7days.app
```

## Missing / Not Working

| Item | Notes |
|------|-------|
| **CG backgrounds** | Prologue backgrounds compressed; likely decoded by JZ4740 IPU hardware (not emulated) — top priority to investigate |
| **Audio output** | `waveout_write` is a no-op; SDL audio device never opened; samples discarded |
| **Save files** | `slot1-3.sav` / `config.sdt` not present; game uses defaults, no persistence |
| **Frame rate cap** | Emulator runs uncapped; no 60fps limiter |
| **OSTimeDly accuracy** | Returns immediately (no-op); simulate_vsync provides approximate tick cadence |
| **MXU correctness** | All ops handled without unknown-opcode errors, but output values unverified |

## Next Steps

### High Priority
1. **Investigate background blitting** — Disassemble the scene-update function (candidate: `0x80A661C8`)
   to determine whether it writes pixels directly to the framebuffer or writes to JZ4740 IPU MMIO
   registers (base `0xB3080000`). If IPU registers: implement minimal IPU emulation (DMA blit from
   compressed source to framebuffer). If direct CPU stores: trace why they never reach the framebuffer.

2. **Audio output** — Open SDL audio device in `waveout_open`; queue 16-bit stereo PCM in
   `waveout_write`; SDL callback drains to speaker. The game produces continuous PCM — data is there,
   just discarded.

### Medium Priority
3. **Save files** — Implement write path for `slot1-3.sav`; allows testing save/load code paths
4. **config.sdt** — Determine format (likely simple key/value); create stub to test non-default settings

### Lower Priority
5. **Frame rate cap** — Add 60fps limiter in main loop; currently irrelevant as emulator runs slower than real hardware
6. **MXU verification** — Cross-check audio mixing output once SDL audio is active
7. **Performance** — Profile hot paths in `cpu.cpp`; target 10× real-hardware speed for smoother playback

## Relevant Files

| File | Role |
|------|------|
| `emulator/src/main.cpp` | Init, patches, BSS stub, main loop, screenshot milestones |
| `emulator/src/syscalls.cpp` | All 72 GOT handlers, µC/OS-II stubs, vsync/scheduler, input |
| `emulator/src/cpu.cpp` | MIPS32 execute loop, GOT trampoline, `g_detected_fb_addr` interception |
| `emulator/src/display.cpp` | SDL2 window, flip/flip_argb8888, RGB565 format, screenshots |
| `emulator/src/display.h` | Dingoo key codes, Display class |
| `emulator/src/memory.cpp` | Memory map, vaddr translation, raw pointer access |
| `emulator/src/mxu.cpp` | MXU/COP2 instruction implementations |
| `emulator/src/archive.cpp` | SPK archive parser (3,216 entries) |
| `7days.app` | Game binary + resource archive (RESOURCE_OFFSET = 0x150000) |
