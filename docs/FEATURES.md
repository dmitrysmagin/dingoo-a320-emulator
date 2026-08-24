# Features & Implementation Status

Implementation details for the Dingoo A320 emulator. Screenshots of running titles are in [README — Supported games](../README.md#supported-games).

## CPU Emulation

| Feature | Status |
|---------|--------|
| MIPS32 Release 1 (all standard opcodes) | ✅ Complete |
| LWL/LWR with correct little-endian formulas | ✅ Complete |
| COP0 (Count/Compare, Status, Cause, EPC, PRId) | ✅ Complete |
| TLB address translation (TLBP/TLBWI/TLBR/TLBWR) | ❌ Removed — flat KSEG0/KSEG1 identity map used instead |
| ERET, exception handling | ✅ Complete |
| COP1 (FPU) | ❌ Not needed (0 real instructions in any tested app) |
| COP2 / MXU (30+ DSP ops) | ✅ All encountered ops implemented |
| Delay slots | ✅ Full support |
| µC/OS-II task model | ✅ Priority-based cooperative scheduler; AppMain registered as task (prio 5), audio as task (prio 16) |

## Memory Map

```
Physical 0x00000000 – 0x01FFFFFF  = 32 MB DRAM
  KSEG0: 0x80000000 → phys (strip 0x80000000)
  KSEG1: 0xA0000000 → phys (strip 0xA0000000)
  KUSEG / KSEG2/3: identity-mapped to phys (no TLB)

Guest memory layout:
  Load address (origin)  = set by .app header (typically 0x80A00000)
  Stack                  = 0x80C00000 (64 KB, grows down)
  Heap                   = after BSS end up to 0x80C00000
```

## Display

| Feature | Status |
|---------|--------|
| 320×240 internal framebuffer, 3× SDL2 window (960×720) | ✅ Complete |
| RGB565 direct rendering | ✅ Complete |
| ARGB8888 → RGB565 conversion | ✅ Complete |
| 8-bit indexed palette + CLUT (256×32-bit at phys 0x13050100) | ✅ Complete |
| Pixel format tracking (`m_lcd_bpp`) stored in C++ member, never guest RAM | ✅ Complete — prevents heap corruption of the format byte |
| Double-buffering (front/back via `_lcd_set_frame` / `_lcd_get_frame`) | ✅ Complete |
| Palette CLUT write handler at 0x13050100 | ✅ Complete |
| DMA controller logging (phys 0x10042000) | ✅ Complete |
| Screenshot (F12 → `screenshots/<game><n>.png` at 320×240; `--save-screenshots` BMP dumps) | ✅ Complete |
| Offscreen/headless mode (SDL_VIDEODRIVER=offscreen) | ✅ Complete |
| `--rotate 90` / `-90` / `270` (SDL window + D-pad; screenshots stay 320×240) | ✅ Complete |

## Audio

| Feature | Status |
|---------|--------|
| SDL2 audio device (16-bit signed LE; rate/channels from `waveout_open`) | ✅ Complete |
| Waveout API (`waveout_open`/`write`/`close`/`can_write`/`reset`/`get_volume`) | ✅ Complete |
| Lock-free SPSC ring buffer (CPU producer, SDL callback consumer) | ✅ Complete |
| Playback clock + ahead-of-playback cap (default 80 ms, `--audio-latency`) | ✅ Complete |
| Scheduler-cooperative blocking when ring/latency full (no host-thread sleep) | ✅ Complete |
| Sem-aware pacing (playback posts buffer sems; suppress spurious `OSSemPost`) | ✅ Complete |
| Underrun fills with silence | ✅ Complete |
| Volume control (`waveout_set_volume`, `HP_Mute_sw`, `_waveout_*` wrappers) | ✅ Complete |
| `pcm_ioctl` (rate, channels, pause, `PCM_GET_SPACE`, volume, mute) | ✅ Complete |
| All-or-nothing writes (full chunk or reject/block; no partial writes) | ✅ Complete |

## Input

| Feature | Status |
|---------|--------|
| Full SDL keyboard → Dingoo bitmask mapping | ✅ Complete |
| `KEY_STATUS` struct (`_kbd_get_status`) conforms to Dingoo SDK (3 fields, hw bit positions) | ✅ Complete |
| `_kbd_get_key` returns Dingoo SDK keycode (0x01–0x0C) | ✅ Complete |
| `_sys_judge_event` returns `(type<<8)\|code` per SDK | ✅ Complete |
| Direct GPIO scan key state at kernel memory addresses | ✅ Complete |
| Offscreen/headless mode support | ✅ Complete |

## Filesystem

| Feature | Status |
|---------|--------|
| `fsys_fopenW`/`fread`/`fseek`/`ftell`/`fclose` | ✅ Complete — handles are 1-based (0 = failure/NULL, 1+ = valid) |
| Resource archive parsing (.spk, up to 3216 entries) | ✅ Complete |
| Path normalization (`a:\`, `.\`, `/` and `\`) | ✅ Complete — relative to uOS2 cwd |
| External save/load | ✅ `home/<game>/` is the guest current directory. `fsys_fopen`/`fopenW` read and write host files (`.\record*.s3dzyz`, etc.); parent dirs are created |
| `fsys_findfirst`/`findnext` | ✅ Lists the mapped home directory |
| `fsys_mkdir`/`remove`/`rename` | ✅ Operate on `home/<game>/` |

## Syscall API Coverage

The emulator intercepts all GOT trampoline calls from the guest binary. The dispatch table has 180 entries covering all 173 documented Dingoo OS functions plus extras:

- **97 implemented** — real host implementations (malloc, printf, LCD, audio, input, µC/OS-II scheduler, filesystem I/O, PCM ioctl, µC/GUI window manager, `dl_res_*`, `dl_load`/`dl_free`/`dl_get_proc`, etc.)
- **83 stubs** — print `[STUB]` and return (TV, accelerometer, audio/video framework, wide-FS, extra libc, and misc categories)
- Standard 72-entry GOT apps are fully dispatched. Apps with extended GOT (Life, StopWatch, dicer with 172 imports; Yi-Chi/Overlord-Fighter with 96) are now covered for all known Dingoo OS functions

Arguments beyond the fourth are read from the caller's stack following the o32 ABI: the
caller reserves 16 bytes of shadow space for `$a0`–`$a3`, so the 5th argument lives at
`$sp+16`. `WM_CreateWindow` is currently the only 7-argument syscall.

---

## App Compatibility

Games under `games/`. Screenshots (three frames per title) are in [README — Supported games](../README.md#supported-games).

| Game | Status |
|------|--------|
| **7days** (HellStriker) | Playable, no sound |
| **AliBaba** | Loads ERPT resources, then KUSEG jump |
| **Block Breaker** | Playable, with sound |
| **Candy** | Playable, with sound |
| **Decollation Warrior** | Playable, with sound |
| **dicer** | Renders title (`Dicer v0.0` / `1d6`) |
| **Formula-One** | Renders (Game & Watch-style F1) |
| **Hell Striker II** | Playable, with sound |
| **Landlord** | Black screen, crashes |
| **Life** | Renders (172-import GOT, same fixes as dicer) |
| **Link'em Up** | Playable |
| **Manic-Miner** | Black screen |
| **Mine Sweeper** | Black screen, then playfield |
| **Mojo** | Playable |
| **Mushroom Roulette** | Renders |
| **Nose Breaker** | Title screen with sound |
| **Overlord-Fighter** / **Yi-Chi King Fighter** | Same FlyApp (`Overlord-Fighter.app` and `Yi-Chi King Fighter (Chinese).app`). Splash then menu; `flydata.dlx` (DLX2) |
| **Platinum Sudoku** | Renders |
| **PoPo Bash** / **Puzzle Bobble** | Same game (`Puzzle Bobble - Popo Bash (Chinese).app`). Renders |
| **Rick-Dangerous** | Renders with sound |
| **Rubido** | Renders |
| **snake** | Renders |
| **StopWatch** | Renders full-screen UI |
| **tetris** | Playable, with sound |
| **ultimate_drift** | Renders |
| **Zhao Yun Chuan** (Chinese) | Renders |
| **Zhao Yun Chuan** (English) | Renders |

**7days** and **tetris** are verified titles — both boot with rendered graphics and stable real-time audio (scheduler block + sem pacing).

---

## Known Limitations

| Issue | Status |
|-------|--------|
| Most games stop before rendering loop | 🔍 Root cause varies: missing resources, GOT gaps, or title-specific logic |
| External files / saves | ✅ `home/<game>/` is the uOS2 current directory (`fsys_fopen`/`fwrite`/`mkdir`/`findfirst`) |
| MXU audio mixing correctness unverified | ⚠️ Low priority |
| `get_current_language` returns 2 (English) | 7days.app contains both `.\ui\` (Chinese) and `.\uien\` (English) and selects English when the firmware language is 2. Return 0 for Simplified Chinese. |
| `ERPT` / SIZED resource archives | ✅ Parsed (`u32` count, 508-byte name/size/offset records, XOR 0x40). PoPo Bash (Puzzle Bobble), Platinum Sudoku, Mushroom Roulette render. Overlord / Yi-Chi (same FlyApp) load `.dlx` via `dl_res_*` |
| `dl_load` / DLX2 modules | ✅ `dl_load` maps archive/sidecar files into guest RAM. Overlord/Yi-Chi parse DLX2 through `dl_res_get_data` + `U8TOU32`. Splash and subsequent frames render; mixer task uses µC/OS-II `OS_TaskReturn` so it no longer KUSEG-halts |

---

## Stubs (83 entries)

All 83 stubs print `[STUB]` and return. Categories:

| Category | Functions |
|----------|-----------|
| TV out | `tv_open/close`, `tv_enable/disable_switch`, `tv_get/set_openflag`, `tv_get/set_closeflag`, `isTVON` |
| Accelerometer | `Custom_Memsic_test`, `Memsic_SerialCommInit`, `Get_X`, `Get_Y`, `Read_Acc`, `Read_Acc0` |
| Audio/video framework | Queue/object leftovers: `av_queue_abort/end/flush/get/init/put`, `av_reg/unreg_object`. Sem/flag/thread/delay, `av_resize_packet`, `av_uft8_2_unicode`, `av_upper_4cc` are implemented |
| Wide filesystem | `fsys_fcloseW`, `fsys_fclose_flash`, `fsys_fopen_flash`, `fsys_renameW` |
| Extra libc | `sscanf`, `vsprintf`, `_tcscmp`, `_tcscpy`, `serial_puts` (`memcpy` / `memset` implemented) |
| Low-level OS | `SysDisableBkLight`, `sys_get_ccpmp_config`, `detect_clock`, `delay_ms`, `udelay` |
| Pre-existing stubs | `vxGoHome`, `free_irq`, `fsys_RefreshCache`, `fsys_flush_cache`, `__icache_invalidate_all`, `__dcache_writeback_all`, `TaskMediaFunStop`, `serial_getc`, `USB_Connect`, `USB_No_Connect`, `udc_attached` |

These stubs unblock all tested apps (including Life, StopWatch, dicer with 172-import GOT) from hitting "Unknown GOT" errors.

## Non‑standard GOT apps

| App | Imports | Status |
|-----|---------|--------|
| Yi‑Chi King Fighter / Overlord‑Fighter (same game) | 96 | All 96 names dispatched. `flydata.dlx` loads (DLX2); splash and menu render; `open_gui_key_msg` posts `WM_KEY` so D-pad/A/Start work |
| Life, StopWatch, dicer | 172 | Phase 1 returns. Slot 166 overridden to `OSTimeGet` (libc timer jal). dicer/Life/StopWatch render |

---

## Debugging & Diagnostics

- **GOT call counts** printed every 1000 frames (`--verbose` / default output)
- **Unmapped hardware register accesses** logged on first occurrence
- **Frame stats** (PC, instruction count, rendered count, GOT calls)
- **Instruction trace** (`CPU::print_trace`, last 256 PCs and opcodes)
- **Register dump** at emulation end
- **Auto‑press schedule** available (see `main.cpp` for example pattern)

---

## File Reference

| Path | Role |
|------|------|
| `../src/main.cpp` | Init, main loop, patches, screenshots |
| `../src/cpu.cpp` | MIPS32 interpreter, opcode dispatch, GOT trampoline handling |
| `../src/cpu.h` | CPU struct, register layout, function declarations |
| `../src/cop0.cpp` | COP0 register handling, ERET |
| `../src/cop0.h` | CP0 register definitions |
| `../src/mxu.cpp` | MXU/COP2 custom DSP opcodes |
| `../src/memory.cpp` | Memory map, address translation, LCD/DMA/IPU register logging |
| `../src/memory.h` | Memory class interface |
| `../src/syscalls.cpp` | All 175 GOT handlers (all Dingoo OS APIs), µC/OS-II scheduler, LCD format mirror, frame buffer pool |
| `../src/syscalls.h` | Syscall dispatch declarations |
| `../src/display.cpp` | SDL2 window, LCD framebuffer, format conversion, key mapping |
| `../src/display.h` | Dingoo key codes, Display class |
| `../src/archive.cpp` | SPK archive parser |
| `../src/app_parser.cpp` | CCDL/IMPT/EXPT/RAWD header parser |
| *(—)* | Full GOT coverage: 175 entries covering all 173 Dingoo OS functions |

---

## Recent Architectural Changes

### 1-based file handles (fsys_fopenW / fsys_fopen)

Previously `alloc_file_handle()` returned 0 for the first free slot. Both `fsys_fopen` and `fsys_fopenW` returned this raw index directly to the guest. Because the Dingoo SDK treats a return value of 0 as `NULL` (open failed), the game silently skipped every resource read — resulting in a white screen with no sprite content.

**Fix:** all open functions now return `idx + 1` to the guest. All internal consumers (`do_fread`, `do_fwrite`, `do_fseek`, `do_ftell`, `do_feof`, `do_ferror`, `close_file_handle`) subtract 1 before indexing into `m_files[]`. Guest handle 0 is unambiguously "failure"; guest handle ≥ 1 is a valid open file. This unlocked full resource loading for 7days and tetris.

### Pixel format out of guest RAM (`m_lcd_bpp`)

The LCD pixel format byte (1 = indexed, 2 = RGB565, 4 = ARGB8888) was previously written to a fixed guest physical address (`0x00508FC0`) so that `_lcd_set_frame` could read it back. That address fell inside the heap zone, and the game's own `malloc` calls silently overwrote it — producing `bpp = 0xFFFFFFFF`, a buffer-size overflow, and every frame being discarded.

**Fix:** pixel format is now tracked exclusively in the C++ member `m_lcd_bpp` (initialised to 2 = RGB565). It is never written to guest RAM. `_lcd_set_frame` and `lcd_get_bpp` read `m_lcd_bpp` directly.

### AppMain registered as a µC/OS-II task

Previously, Phase 2 simply jumped to AppMain and called it as a plain function, with no scheduler awareness. The audio task (created via `OSTaskCreate` at priority 16) had no counterpart for AppMain, so scheduling was ad-hoc.

**Fix:** `register_main_context(pc, a0, prio=5)` is called in `main.cpp` immediately before the Phase 2 `cpu.run()` loop. This registers AppMain as task 0 at priority 5 — higher priority than the audio task (priority 16). The cooperative scheduler now correctly yields between AppMain and the audio task on every `OSTimeDly` / `OSSemPend` call.

### Expanded stub coverage to all 173 Dingoo OS functions

The original dispatch table covered 87 entries (indices 0–86). Apps with extended GOT (Life, StopWatch, dicer — 172 imports) hit "Unknown GOT" errors and stopped before rendering.

**Fix:** cross-referenced the full Dingoo OS API list (173 functions) and added the missing entries to `s_handlers[]`. The table now has 180 entries. All tested apps, including 172-import GOT apps, avoid "Unknown GOT" errors. 91 functions have real implementations; 89 are stubs that print `[STUB]` and return.

### System-model check (`cmGetSysModel`)

Overlord-Fighter exited within two frames. Its `AppMain` calls a guard routine that
`memset`s a 100-byte buffer, passes it to `cmGetSysModel`, converts the result with
`__to_locale_ansi(wchar_t*)`, and `strcmp`s it against `"GM760"` and `"A320"`. Neither
matched, so the routine fell through, `AppMain` returned 1, and the sentinel fired.

`cmGetSysModel` was a stub that wrote an ASCII string to a fixed scratch address and
returned a pointer, leaving the caller's buffer zeroed. **Fix:** `cmGetSysModel` and
`cmGetSysVersion` now serve both calling conventions — they fill `$a0` with a UTF-16LE
string when it is a writable guest pointer, and still return a static ASCII copy for
callers that pass no argument.

### Minimal µC/GUI window manager

Yi-Chi and Overlord-Fighter create one full-screen window with a callback, then poll
`GUI_Exec()` forever. With `GUI_Exec` stubbed the callback never ran, so the games spun
without producing frames (13.5M `GUI_Exec` calls in 15 seconds, 1 frame rendered).

**Fix:** `GUI_Exec` now drives a real message pump. `WM_CreateWindow` records the
callback and queues `WM_CREATE` (id 1); the first `GUI_Exec` delivers it, and later calls
fire due `GUI_TIMER_*` timers, whose callbacks post `WM_TIMER` (id 0x113) back through
`WM__SendMessage`. After `open_gui_key_msg`, keypad edges are posted as `WM_KEY` (id 14)
with a `WM_KEY_INFO { Key, PressedCnt }` whose `Key` values are µC/GUI `GUI_KEY_*`
(ENTER=13, arrows 16–19, ESCAPE=27). Messages use the µC/GUI layout `{ int MsgId;
U16 hWin; U16 hWinSrc; U32 Data; }` and are delivered with `call_guest_function`, which
reuses the existing return stub at `0x80BFFF00`. Both titles now boot, render, and
accept menu input.

Two supporting bugs were fixed alongside it: stack-passed syscall arguments were read 16
bytes too high (see the ABI note above), and the callback return stub popped 8 bytes from
a 16-byte frame, leaking stack on every guest callback.

### `--seconds` now covers Phase 1

The run deadline was only checked in the Phase 2 loop, so a title whose `dl_main` never
returned (dicer, Life, StopWatch) ignored `--seconds` and hung indefinitely. The deadline
is now established before Phase 1 and checked in both loops.

### Wall-clock-paced µC/OS-II ticks

Previously `m_os_ticks` advanced by 1 per emulator "frame" (`simulate_vsync` call) — a variable rate that depended on host speed. `OSTimeDly(60)` (intended 1-second delay) completed in milliseconds on a fast host, making animations and timeouts run far faster than real-time.

**Fix:** `simulate_vsync` now batches OS ticks to wall-clock time:
```cpp
u32 expected_ticks = (SDL_GetTicks() - m_start_tick) * 60 / 1000;
while (m_os_ticks < expected_ticks) {
    m_os_ticks++;
    // wake timed-out tasks per tick
}
```
This gives `OSTimeGet` / `OSTimeDly` / `OSSemPend(timeout)` correct real-time behaviour without adding threads or SDL timers. GUI_TIMER_* SW timers already used wall-clock time via `SDL_GetTicks()` — both timebases are now consistent.

### Startup sentinel clearing in `_sys_judge_event`

The kernel pre-populates `EVENT_QUEUE_ADDR` (0x80BFECD8) with `0x8BFC4D89` as a hardware-ready flag before dl_main runs. When `_sys_judge_event` read this value and returned it to the game, the game interpreted it as an OS exit signal — triggering immediate audio teardown and shutdown after two frames.

**Fix:** `_sys_judge_event` silently clears the sentinel value (`0x8BFC4D89 → 0`) without returning it to the game. Any other non-zero queued event is still returned normally.

---

## References

- **JZ4730 SoC**: Ingenic JZ4730 (MIPS32 4Kc-like core, little-endian)
- **Dingoo SDK**: `elf2app` tool wraps MIPS ELF into `.app` format
- **Display**: 320×240 RGB565 LCD, double-buffered via LCD controller DMA
- **Audio**: Waveout API, 16-bit signed PCM, 44100 Hz, stereo, mixed via MXU DSP
- **OS**: µC/OS-II real-time kernel (task creation, semaphores, time services)
- **Archive format (`.spk`)**: `uint16 LE count` + `count × (char[64] name + uint32 LE offset)` + data blocks
- **`.app` format**: CCDL/IMPT/EXPT/RAWD 4-header structure; resource section appended after RAWD
- **`KEY_STATUS` struct** (Dingoo SDK `keyboard.h`): 3 × `unsigned long` — `pressed` (+0), `released` (+4), `status` (+8). Bit positions are game-specific hardware constants (AstroLander `control.h`), not DKEY_* values
- **Input API**: `_kbd_get_status(KEY_STATUS*)` fills the struct with hw-bit current state; `_kbd_get_key()` returns a Dingoo SDK keycode (0x01–0x0C); `_sys_judge_event(void*)` returns `(type<<8)\|code` or 0
