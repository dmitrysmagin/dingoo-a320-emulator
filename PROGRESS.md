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
- **Current pipeline** — `_lcd_set_frame(end_ptr)` uses real OS format mirror:
  - Format pixel width read from OS mirror at `0x80508FC0` (set during `LcdGetDisMode` boot init)
  - Palette flag read from `0x806A1DDC` (0 = RGB565/ARGB, non-zero = 8-bit indexed + CLUT)
  - BPP determined: 1 (palette), 2 (RGB565), 4 (ARGB8888), etc.
  - ARGB8888 detected via format mirror → converted to RGB565 via pool buffer
  - 8-bit indexed palette mode → CLUT lookup at `0x13050100` (256×32-bit entries) → RGB565
  - Direct RGB565 → memcpy to pool buffer, flip
- Format mirror and palette flag initialised at boot to default values (pixel_width=2, pal_flag=0)
- `_lcd_get_frame()` returns back buffer (KSEG1), allocates from guest heap if needed
- `lcd_get_cframe()` returns current front buffer (KSEG0)
- Frame buffer pool (`allocate_fb`/`release_fb`) for temporary RGB565 conversion targets
- Double-buffering tracked via front/back address swap in display
- RGB565 → ARGB8888 conversion with proper 5→8 and 6→8 bit replication (top bits fill LSBs)
- Screenshot auto-save: frames 1–10, then every 10th frame thereafter
- F12 manual screenshot; Escape to quit

### Scheduling & Tasks
- µC/OS-II tasks are **registration-only**: `OSTaskCreate` stores the entry/prio/stack for
  Pend/Post bookkeeping, but the task function is never dispatched as an independent context.
  The game calls the audio task at `0x80A06400` **inline** from `game_main` through a vtable
  pointer — the scheduler is never invoked for context switching.
- `OSSemPend` / `OSSemPost` synchronise the inline audio pipeline. When Pend blocks (count =
  0) with no other ready task, it returns `OS_TIMEOUT` immediately (no background task to
  switch to); the caller handles the error and retries.
- `simulate_vsync()` auto-start registers the first created task (sets `m_current_task` for
  Pend bookkeeping) but does **not** switch registers or PC — game_main continues inline.
- Duplicate priority rejection (real µC/OS-II behaviour — `OS_PRIO_EXIST = 40`). The game
  retries with `prio = 17` on failure.
- `find_ready_task` uses priority ordering (lowest `task_prio` value = highest urgency).
- Cooperative yield in `simulate_vsync` checks for a higher-priority ready task; typically
  finds none since all processing is inline.
- `m_start_tick` initialised in constructor for potential real-time tick use (currently
  unused — ticks are frame-based, `m_os_ticks += 1` per vsync).

### Input
- Full SDL keyboard → Dingoo bitmask mapping (Z=A, X=B, A=X, S=Y, Q=L, W=R, Enter=START, Tab=SELECT)
- `_kbd_get_status` writes key bitmask to both `$v0` register **and** guest RAM at `0x80B49D08`
  (the second write was the root cause fix for text never advancing)
- Event queue at `0x80BFECD8`; `_sys_judge_event` reads and clears it each call
- No synthetic auto-press or event injection — all keys come from real SDL window input
- `SDL_VIDEODRIVER=offscreen` + software renderer fallback for headless/CI testing

### Audio
- SDL2 audio device opened in `waveout_open` with guest-specified rate/channels/bits (16-bit signed LE)
- `waveout_write` reads PCM samples from guest RAM and pushes to a mutex-protected queue
- SDL audio callback drains the queue to the host speaker; silence on underrun
- `waveout_can_write` reports available queue space (up to 64KB) instead of hardcoded 4096
- Audio task runs correctly — `waveout_write` called ~22M times per 9,990-frame test run
- Clean shutdown: `shutdown_audio()` closes device and destroys mutex

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
| Duplicate task priority | `syscalls.cpp` | Reject with `OS_PRIO_EXIST` (matching real µC/OS-II); game retries with `prio=17` |
| Scheduler lazy start | `syscalls.cpp` | `simulate_vsync()` registers first task for Pend bookkeeping but does NOT switch context |
| Code section protection | `main.cpp` | Removed — game's idle/task stacks overlap RAWD/BSS boundary |
| Screenshot bit expansion | `display.cpp` | SDL `ConvertSurfaceFormat` replaces manual loop; white → (255,255,255) not (248,252,248) |
| SDL software renderer fallback | `display.cpp` | Allows `SDL_VIDEODRIVER=offscreen` for headless tests |
| `_kbd_get_status` dual-write | `syscalls.cpp` | Also writes keys to guest RAM `0x80B49D08`; without this text never advanced |
| SDL audio output | `syscalls.cpp`/`display.cpp` | Opens SDL2 audio device in `waveout_open`; queues PCM in `waveout_write`; drains via callback |
| `g_detected_fb_addr` removed | `cpu.cpp`/`syscalls.cpp` | Replaced by proper FB pool + format mirror in `_lcd_set_frame` |
| LCD format mirror init | `syscalls.cpp` | Writes pixel_width=2 and pal_flag=0 to OS runtime mirrors at boot |
| DMA controller logging | `memory.cpp` | Added `log_dma_write()` for phys 0x10042000–0x100420FF |
| LCD reg names corrected | `memory.cpp` | Fixed JZ4740 LCD register map (CTRL, CFG2, DAH, DBA, DBB) |
| FB pool + format conv | `syscalls.cpp` | `allocate_fb()/release_fb()`, `argb8888_to_rgb565()`, palette CLUT support |

## Current Status

### Game Progression
- Logo, intro CG, and game menu render correctly
- Dialogue text overlay advances properly through the prologue
- Format mirror fix ensures `_lcd_set_frame` correctly interprets the pixel format
- No crashes, no unknown opcodes across the entire run
- Synthetic auto-press removed — all key input comes from real SDL window interactions

### Open Problem: Prologue CG Backgrounds
Three large resource reads occur at startup (before/during prologue):
- 77,240 bytes at file offset ~`0x02EB63FC` (prologue background 1)
- 77,324 bytes (prologue background 2)
- 90,888 bytes (prologue background 3)

These are compressed images (far smaller than 153,600 bytes for an RGB565 frame). The game
decompresses them before writing to the framebuffer. If prologue backgrounds fail to appear
while text overlay works, the root cause is likely in the decompression or write path:

- **TLB hypothesis**: Game may use TLB-mapped KUSEG for decompression output. TLB stubs in
  `cop0.cpp` are no-ops, so writes land in identity-mapped physical addresses instead.
  **But** TLB instructions counted in `docs/TLB.md` are in the resource section, not code
  section — game may not execute them at all during boot/prologue.
- **Decompression format**: `.spl`/`.sst`/`.sbp` formats may use palette/indexed color modes
  that depend on CLUT at `0x13050100`. Palette support is stubbed but untested.
- **Buffer address**: Decompressed data might write to a buffer `_lcd_set_frame` doesn't point to.

### Performance
- ~64M guest instructions/second on modern x86
- 2-minute run: ~9,990 rendered frames, ~52,000 CPU frames at 2M insns/frame
- Audio handled inline: `waveout_write` ~58K calls per 300-frame test run

### Run Command
```
cd emulator
SDL_VIDEODRIVER=offscreen timeout 60 ./emulator.exe ../7days.app
```

## Missing / Not Working

| Item | Notes |
|------|-------|
| **Prologue CG backgrounds** | Compressed `.spl`/`.sst` images; TLB or decompression issue suspected |
| **Save files** | `slot1-3.sav` / `config.sdt` not present; game uses defaults, no persistence |
| **Frame rate cap** | Emulator runs uncapped; no 60fps limiter |
| **MXU correctness** | All ops handled without unknown-opcode errors, but output values unverified |

## Next Steps

### High Priority
1. **Prologue CG debugging** — Determine root cause of black backgrounds:
   - Trace decompression output writes (capture buffer addresses and contents)
   - Verify CLUT palette data at `0x13050100` is populated for indexed-mode CGs
   - Check if game uses TLB-mapped addresses for decompression targets
2. **Palette/CLUT write handler** — Implement proper storage for LCD controller CLUT writes
   at phys `0x13050100` (256 × 32-bit entries) in `memory.cpp`

### Medium Priority
1. **Save files** — Implement write path for `slot1-3.sav`; allows testing save/load code paths
2. **config.sdt** — Determine format (likely simple key/value); create stub

### Lower Priority
3. **Frame rate cap** — Add 60fps limiter in main loop
4. **MXU verification** — Listen for correct audio output; verify MXU mixing
5. **Performance** — Profile hot paths in `cpu.cpp`; target 10× real-hardware speed

## Relevant Files

| File | Role |
|------|------|
| `emulator/src/main.cpp` | Init, patches, BSS stub, main loop, screenshot milestones |
| `emulator/src/syscalls.cpp` | All 72 GOT handlers, µC/OS-II stubs, vsync/scheduler, input, LCD format mirror, FB pool |
| `emulator/src/cpu.cpp` | MIPS32 execute loop, GOT trampoline |
| `emulator/src/display.cpp` | SDL2 window, flip/flip_argb8888, RGB565 format, screenshots |
| `emulator/src/display.h` | Dingoo key codes, Display class |
| `emulator/src/memory.cpp` | Memory map, vaddr translation, raw pointer access, LCD/DMA/IPU register logging |
| `emulator/src/mxu.cpp` | MXU/COP2 instruction implementations |
| `emulator/src/archive.cpp` | SPK archive parser (3,216 entries) |
| `7days.app` | Game binary + resource archive (RESOURCE_OFFSET = 0x150000) |
