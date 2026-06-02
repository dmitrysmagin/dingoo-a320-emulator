# Dingoo A320 Emulator

A portable MIPS32 interpreter-based emulator for Dingoo A320 (JZ4730 SoC) `.app` games.

Runs any standard `.app` binary with Dingoo OS syscall interception, SDL2 display/audio/input, and resource archive support.

---

## Architecture

```
┌──────────────────────────────────────────────────────────────────┐
│                      Host (SDL2 + your OS)                       │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │                    Emulator Core                          │   │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌─────────┐ │   │
│  │  │ MIPS32   │  │ MXU      │  │ Memory   │  │ COP0    │ │   │
│  │  │ Decoder  │  │ (COP2)   │  │ Manager  │  │ (stub)  │ │   │
│  │  │ + Exec   │  │          │  │ (flat    │  │         │ │   │
│  │  │          │  │          │  │  KSEG0/1)│  │         │ │   │
│  │  └──────────┘  └──────────┘  └──────────┘  └─────────┘ │   │
│  │                                                          │   │
│  │  ┌──────────────────────────────────────────────────┐   │   │
│  │  │           Dingoo OS Syscall Interception         │   │   │
│  │  │ 72+ high-level function implementations via host  │   │   │
│  │  └──────────────────────────────────────────────────┘   │   │
│  │                                                          │   │
│  │  ┌──────────────────────────────────────────────────┐   │   │
│  │  │           Resource Archive Provider              │   │   │
│  │  │  Serves .spk entries from embedded archive data  │   │   │
│  │  └──────────────────────────────────────────────────┘   │   │
│  └──────────────────────────────────────────────────────────┘   │
│                                                                  │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌────────────────┐   │
│  │ SDL2     │  │ SDL2     │  │ SDL2     │  │ Host FS        │   │
│  │ Video    │  │ Audio    │  │ Events   │  │ (save files)   │   │
│  │ (window) │  │ (PCM out)│  │ (keyboard)│  │                │   │
│  └──────────┘  └──────────┘  └──────────┘  └────────────────┘   │
└──────────────────────────────────────────────────────────────────┘
```

### Core components

| Module | File(s) | Role |
|--------|---------|------|
| MIPS32 interpreter | `cpu.cpp`, `cpu.h` | Fetches, decodes, executes all standard MIPS32 r1 opcodes |
| COP0 | `cop0.cpp`, `cop0.h` | MIPS32 CP0 register handling, TLB-emulation-free mode |
| MXU (COP2) | `mxu.cpp`, `cpu.cpp` | Dingoo DSP coprocessor (30+ ops for audio mixing, fixed-point math) |
| Memory manager | `memory.cpp`, `memory.h` | Flat KSEG0/KSEG1 address map, identity-mapped KUSEG |
| Syscall dispatch | `syscalls.cpp`, `syscalls.h` | Intercepts GOT trampoline calls → host implementations |
| Display | `display.cpp`, `display.h` | SDL2 window, LCD framebuffer, format conversion, key mapping |
| Archive loader | `archive.cpp`, `archive.h` | Parses Dingoo `.spk` archive format |
| App parser | `app_parser.cpp`, `app_parser.h` | Parses `.app` file header (CCDL/IMPT/EXPT/RAWD) |

---

## Building

### Requirements

- **C++17 compiler** (GCC/MinGW-w64 recommended on Windows)
- **SDL2** development libraries
- **MinGW/MSYS2** (on Windows) or equivalent

### Build (Windows/MinGW)

```bash
cd emulator
g++ -pipe -std=c++17 -Wall -Wextra -O2 -g \
  -I/path/to/SDL2/include -Dmain=SDL_main \
  -o emulator.exe src/main.cpp src/app_parser.cpp src/memory.cpp \
  src/cpu.cpp src/cop0.cpp src/syscalls.cpp src/mxu.cpp \
  src/display.cpp src/archive.cpp \
  -L/path/to/SDL2/lib -lmingw32 -lSDL2main -lSDL2
```

Or use the included `Makefile` (check paths for your SDL2 installation):

```bash
cd emulator
make
```

---

## Usage

```bash
./emulator.exe [options] <game.app>

Options:
  --frames <n>        Stop after n CPU frames (0 = unlimited)
  --save-screenshots  Save BMP screenshots periodically
  --no-sound          Disable audio
  --quiet             Suppress diagnostic output (log only errors)
```

### Examples

```bash
./emulator.exe ../7days.app
./emulator.exe --frames 5000 --save-screenshots ../tetris.app
SDL_VIDEODRIVER=offscreen ./emulator.exe --frames 1000 ../snake.app
```

### Controls

Key bit positions within `KEY_STATUS.status` follow the Dingoo SDK convention
(used by AstroLander `control.h` constants).

| Dingoo button | SDL/Keyboard key | HW bit |
|---------------|------------------|--------|
| A             | Z                | 31     |
| B             | X                | 21     |
| X             | A                | 16     |
| Y             | S                | 6      |
| L             | Q                | 8      |
| R             | W                | 29     |
| START         | Enter            | 11     |
| SELECT        | Tab              | 10     |
| D-Pad Up      | ↑                | 20     |
| D-Pad Down    | ↓                | 27     |
| D-Pad Left    | ←                | 28     |
| D-Pad Right   | →                | 18     |
| Volume +/-    | = / -            | —      |
| Quit          | Escape           | —      |

---

## Features & Implementation Status

### CPU Emulation

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
| µC/OS-II task model | ✅ Cooperative single-threaded (tasks registered but not preemptively scheduled) |

### Memory Map

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

### Display

| Feature | Status |
|---------|--------|
| 320×240 internal framebuffer, 3× SDL2 window (960×720) | ✅ Complete |
| RGB565 direct rendering | ✅ Complete |
| ARGB8888 → RGB565 conversion | ✅ Complete |
| 8-bit indexed palette + CLUT (256×32-bit at phys 0x13050100) | ✅ Complete |
| Format mirror detection (reads pixel format from OS runtime data) | ✅ Complete |
| Double-buffering (front/back via `_lcd_set_frame` / `_lcd_get_frame`) | ✅ Complete |
| Palette CLUT write handler at 0x13050100 | ✅ Complete |
| DMA controller logging (phys 0x10042000) | ✅ Complete |
| Screenshot auto-save (F12 manual, or --save-screenshots) | ✅ Complete |
| Offscreen/headless mode (SDL_VIDEODRIVER=offscreen) | ✅ Complete |

### Audio

| Feature | Status |
|---------|--------|
| SDL2 audio device (16-bit signed LE, 44100 Hz, stereo) | ✅ Complete |
| Waveout API (`waveout_open`/`write`/`close`/`can_write`) | ✅ Complete |
| Mutex-protected PCM sample queue | ✅ Complete |
| Underrun fills with silence | ✅ Complete |
| Volume control (`waveout_set_volume`, `HP_Mute_sw`) | ✅ Complete |
| Output correctness | ⚠️ Unverified — MXU mixing and TLB-free addressing may affect PCM fidelity |

### Input

| Feature | Status |
|---------|--------|
| Full SDL keyboard → Dingoo bitmask mapping | ✅ Complete |
| `KEY_STATUS` struct (`_kbd_get_status`) conforms to Dingoo SDK (3 fields, hw bit positions) | ✅ Complete |
| `_kbd_get_key` returns Dingoo SDK keycode (0x01–0x0C) | ✅ Complete |
| `_sys_judge_event` returns `(type<<8)\|code` per SDK | ✅ Complete |
| Direct GPIO scan key state at kernel memory addresses | ✅ Complete |
| Offscreen/headless mode support | ✅ Complete |

### Filesystem

| Feature | Status |
|---------|--------|
| `fsys_fopenW`/`fread`/`fseek`/`ftell`/`fclose` | ✅ Complete |
| Resource archive parsing (.spk, up to 3216 entries) | ✅ Complete |
| Path normalization (`.\dir\file.ext` → `dir/file.ext`) | ✅ Complete |
| Save file writes (`slot*.sav`, `config.sdt`, `state.sdt`) | ❌ Returns NOT FOUND (game handles gracefully) |
| `fsys_findfirst`/`findnext` | ✅ Real implementation (`opendir`/`readdir` on host) |
| `fsys_remove`/`rename` | ⚠️ Stubbed (returns -1) |

### Syscall API Coverage

The emulator intercepts all GOT trampoline calls from the guest binary. Of ~77 possible imports:

- **52 implemented** — real implementations (malloc, printf, LCD, audio, input, timer, unicode, directory search, SR, etc.)
- **20 stubbed** — return constants (abort, cache ops, USB, volume, etc.)
- **0 unknown** for standard 72-entry GOT apps
- **38 unknown** for Yi-Chi King Fighter (uncommon µC/GUI imports)

---

## App Compatibility

Tested with 14 `.app` files:

| App | Status |
|-----|--------|
| 7days (HellStriker) | ✅ Boots to gameplay, dialogue, menu |
| hsingtin | ✅ Loads and runs |
| Decollation Warrior | ✅ Loads and runs |
| Hell Striker II | ✅ Loads and runs |
| candy | ✅ Loads and runs |
| linkemup | ✅ Loads and runs |
| tetris | ✅ Loads and runs |
| ultimate_drift | ✅ Loads and runs |
| Zhao Yun Chuan | ✅ Loads and runs |
| brick | ✅ Loads and runs |
| snake | ✅ Loads and runs |
| Puzzle Bobble | ✅ Loads and runs |
| Landlord | ✅ Loads and runs |
| Yi-Chi King Fighter | ⚠️ 38 unknown imports (GUI framework) |

---

## Performance

- ~64 million guest MIPS instructions / second on modern x86
- Typically 2M instructions per CPU frame, ~50–60 CPU frames for 1 rendered frame
- Runs approximately 5× slower than real JZ4730 hardware (360 MHz)
- Audio handled inline and asynchronously via SDL callback

---

## Known Limitations

| Issue | Status |
|-------|--------|
| Prologue CG backgrounds may appear black | 🔍 Decompression format investigation pending |
| Save file write path not implemented | ⚠️ Medium priority |
| No frame rate cap (runs as fast as emulator can go) | ⚠️ Low priority |
| MXU audio mixing correctness unverified | ⚠️ Low priority |
| `get_current_language` hardcoded to English | ⚠️ Low priority — may affect Chinese UI locale |

---

## Stubbed / Unimplemented APIs

### Quick‑win stubs (implementable with modest effort)

| GOT | API | Minimal implementation |
|-----|-----|-----------------------|
| 72 | `get_dl_handle` | Allocate dummy handle referencing current Archive |
| 73–76 | `dl_res_*` | Thin wrappers around `Archive::find` |

### Harmless hardware stubs (20 entries)

Compete list in [`unimplemented.md`](unimplemented.md). All return constants with no side effects.

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
| `src/main.cpp` | Init, main loop, patches, screenshots |
| `src/cpu.cpp` | MIPS32 interpreter, opcode dispatch, GOT trampoline handling |
| `src/cpu.h` | CPU struct, register layout, function declarations |
| `src/cop0.cpp` | COP0 register handling, ERET |
| `src/cop0.h` | CP0 register definitions |
| `src/mxu.cpp` | MXU/COP2 custom DSP opcodes |
| `src/memory.cpp` | Memory map, address translation, LCD/DMA/IPU register logging |
| `src/memory.h` | Memory class interface |
| `src/syscalls.cpp` | All 77 GOT handlers, µC/OS-II stubs, LCD format mirror, frame buffer pool |
| `src/syscalls.h` | Syscall dispatch declarations |
| `src/display.cpp` | SDL2 window, LCD framebuffer, format conversion, key mapping |
| `src/display.h` | Dingoo key codes, Display class |
| `src/archive.cpp` | SPK archive parser |
| `src/app_parser.cpp` | CCDL/IMPT/EXPT/RAWD header parser |
| `unimplemented.md` | API coverage gaps (missing + stubbed GOT entries) |
| `STUBS.md` | Quick-win stub implementation ideas |

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
