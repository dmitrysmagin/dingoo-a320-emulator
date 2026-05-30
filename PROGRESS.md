# Progress — 7days Dingoo Emulator

## Goal
Boot the Dingoo game **"7days"** (`7days.app`, SPK archive) to playable gameplay in a custom C++17/SDL2 emulator (MinGW, Windows).

## What Works

### Archive & Loading
- SPK archive parsing (`archive.cpp`) — 3216 resource entries extracted on demand
- RAWD binary loading, BSS zeroing, stack area initialisation
- Resource archive loaded into guest RAM at phys `0x00B50000` (64 KB-aligned after prog_size)
- Game name `"7days"` written as UTF-16LE at `0x80B44FE0` (survives `dl_main` BSS clear via BSS stub)

### CPU Emulation
- MIPS32r1 core, all standard opcodes including LWL/LWR (correct little-endian formulas)
- COP0 stubs (Count/Compare auto-increment per instruction, TLB no-ops)
- COP2 / MXU coprocessor (`mxu.cpp`) — all encountered ops handled; zero unknown-opcode hits in 6.4B instructions
- GOT trampoline dispatch for all 72 import entries
- 6.4 billion instructions executed in a 2-minute run with no crashes

### Display
- 320×240 internal framebuffer, scaled 3× → 960×720 via SDL2
- **Dual-buffer composite rendering**: background from smart-scan winner + text overlay from `g_detected_fb_addr`
  - Smart scan: samples every 128th halfword at 0x4000-step intervals across heap; re-evaluates every 50 `_lcd_set_frame` calls
  - CPU interception at PC `0x80A21E78` tracks render-target address (`g_detected_fb_addr = 0x0011E890`)
  - `flip_composite()` overlays non-zero pixels from text buffer on top of background
- RGB565 format verified correct: Dingoo JZ4740 (little-endian MIPS) and SDL on little-endian x86 use identical packed u16 layout — no byte-swap needed
- Screenshot save uses `SDL_ConvertSurfaceFormat` for proper 5→8 and 6→8 bit expansion (replicates top bits; was truncating, giving white as 248 not 255)
- Auto-save screenshots: frames 1–5 and every 25th frame thereafter

### Scheduling & Tasks
- Dual µC/OS-II task scheduling via preemptive time-slicing in `simulate_vsync()`
- Both tasks get CPU time each VSYNC; same-priority tasks round-robin
- `OSTaskCreate` / `OSSemCreate` / `OSSemPend` / `OSSemPost` all functional

### Input
- Full SDL keyboard → Dingoo bitmask mapping (Z=A, X=B, A=X, S=Y, Q=L, W=R, Enter=START, Tab=SELECT)
- Event queue at `0x80BFECD8`; `_sys_judge_event` reads and clears it each call
- Auto-press schedule for testing: START at vsync 200, then A every 100 vsyncs up to vsync 3000
- `SDL_VIDEODRIVER=offscreen` + software renderer fallback for headless/CI testing

### Audio (partial)
- `waveout_open` / `waveout_write` / `waveout_close` all accepted by audio task
- `waveout_can_write` returns 4096 (always ready) to keep audio task unblocked
- Audio task runs correctly — `waveout_write` called ~737K times per vsync-1000, matching Pend/Post counts
- **No actual audio output** — samples are discarded; see Missing section

### Filesystem
- `fsys_fopenW` / `fsys_fread` / `fsys_fseek` / `fsys_ftell` / `fsys_fclose` implemented
- All resource files served from embedded SPK archive (3216 entries)
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

## Current Status

### Game Progression (as of last test run)
- Boots through `dl_main` → `GameEngineInit` → `AppMain`
- Loads title/intro screen (warm brown background, 100% pixel coverage)
- Transitions to main dialogue scene (teal-green background)
- Scene changes detected and composited correctly (4 distinct backgrounds observed)
- Advances through 700+ rendered frames of dialogue in a 2-minute run
- All rendered frames have unique MD5s — text overlay changing with each dialogue line
- No crashes, no unknown opcodes in 6.4 billion instructions

### Performance
- ~64M guest instructions/second on modern x86 (was 14–20M at PROGRESS.md v1)
- ~5× slower than real JZ4730 at 336 MHz (was 25–30× at v1)
- 2-minute run: ~10B instructions, 700+ rendered frames

### Run Command
```
cd emulator
SDL_VIDEODRIVER=offscreen timeout 60 ./emulator.exe ../7days.app
```

## Missing / Not Working

| Item | Notes |
|------|-------|
| **Audio output** | `waveout_write` is a no-op; SDL audio device never opened; samples discarded |
| **Save files** | `slot1-3.sav` / `config.sdt` not present; game uses defaults, no persistence |
| **Frame rate cap** | Emulator runs uncapped; no 60fps limiter |
| **OSTimeDly accuracy** | Returns immediately (no-op); simulate_vsync provides approximate tick cadence |
| **MXU correctness** | All ops handled without unknown-opcode errors, but output values unverified (no audio playback to check against) |

## Next Steps

### High Priority
1. **Audio output** — Open SDL audio device in `waveout_open`; queue PCM in `waveout_write`; SDL callback drains to speaker. The game produces 16-bit stereo PCM continuously — the data is there, just discarded.

### Medium Priority
2. **Save files** — Implement write path for `slot1-3.sav`; allows testing save/load code paths
3. **config.sdt** — Determine format (likely simple key/value); create stub to test non-default settings

### Lower Priority
4. **Frame rate cap** — Add 60fps limiter in main loop; currently irrelevant as emulator is slower than real hardware
5. **MXU verification** — Cross-check audio mixing output once SDL audio is active; fix any ops that produce wrong values
6. **Performance** — Profile hot paths in `cpu.cpp`; target 10× real-hardware speed for smoother playback

## Relevant Files

| File | Role |
|------|------|
| `emulator/src/main.cpp` | Init, patches, BSS stub, main loop, screenshot milestones |
| `emulator/src/syscalls.cpp` | All 72 GOT handlers, µC/OS-II stubs, vsync/scheduler |
| `emulator/src/cpu.cpp` | MIPS32 execute loop, GOT trampoline, `g_detected_fb_addr` interception |
| `emulator/src/display.cpp` | SDL2 window, flip/flip_composite, RGB565 format, screenshots |
| `emulator/src/display.h` | Dingoo key codes, Display class |
| `emulator/src/memory.cpp` | Memory map, vaddr translation, raw pointer access |
| `emulator/src/mxu.cpp` | MXU/COP2 instruction implementations |
| `emulator/src/archive.cpp` | SPK archive parser (3216 entries) |
| `7days.app` | Game binary + resource archive (RESOURCE_OFFSET = 0x150000) |
