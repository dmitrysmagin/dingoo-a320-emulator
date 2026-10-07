# Dingoo A320 Emulator

A portable MIPS32 emulator for Dingoo A320 (JZ4730 SoC) `.app` games, with
two execution tiers: a reference interpreter and a dynarec JIT
(`--jit=on`, ~4× faster, bit-identical output).

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
 │  │  │           Dynarec JIT (`--jit=on`)                 │   │   │
 │  │  │ cached straight-line + branch TBs, interp fallback│   │   │
 │  │  └──────────────────────────────────────────────────┘   │   │
 │  │                                                          │   │
 │  │  ┌──────────────────────────────────────────────────┐   │   │
│  │  │           Dingoo OS Syscall Interception         │   │   │
│  │  │ 97 implemented + 83 stubs = 180 intercepted     │   │   │
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
| Dynarec JIT | `jit/` (`jit.cpp`, `frontend.cpp`, `emit_*.cpp`) | Cached x86-64 translation blocks (ALU, memory fast path, COP0/COP2 calls, branch exits); interpreter fallback for the rest |
| COP0 | `cop0.cpp`, `cop0.h` | MIPS32 CP0 register handling, TLB-emulation-free mode |
| MXU (COP2) | `mxu.cpp`, `cpu.cpp` | Dingoo DSP coprocessor (30+ ops for audio mixing, fixed-point math) |
| Memory manager | `memory.cpp`, `memory.h` | Flat KSEG0/KSEG1 address map, identity-mapped KUSEG |
| Syscall dispatch | `syscalls.cpp`, `syscalls.h` | Intercepts GOT trampoline calls → host implementations; owns µC/OS-II task scheduler |
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
  --nosound           Disable audio output
  --audio-latency <ms>  Max queued audio ahead of playback (default 80, range 20–500)
  --rotate <deg>      Rotate the SDL window and D-pad (90, -90, or 270)
  --jit={off,on}      Execution tier: interpreter reference (default) or dynarec JIT
  --jit-stats         Print JIT cache/TB counters at exit (implies nothing else)
  --jit-tests         Run the JIT discharge test suite (no ROM) and exit
```

### Examples

```bash
./emulator.exe ../7days.app
./emulator.exe --frames 5000 --save-screenshots ../tetris.app
./emulator.exe --audio-latency 60 ../tetris.app
./emulator.exe --rotate 90 games/tetris.app
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
| Screenshot    | F12              | —      |
| Quit          | Escape           | —      |

F12 writes a PNG of the native 320×240 framebuffer (not the scaled or rotated window) to
`screenshots/<game_name><n>.png`, for example `screenshots/tetris1.png`. The
number increments so existing files are not overwritten.

`--rotate 90` turns the window clockwise (portrait 240×320) and remaps the D-pad
so arrow keys follow the screen. `--rotate -90` and `--rotate 270` are
counter-clockwise (the same transform). A/B/X/Y and other buttons are unchanged.

### JIT mode

`--jit=on` executes games through cached x86-64 translation blocks instead of
the interpreter loop: straight-line ALU runs, a RAM fast path for loads/stores
(MMIO/unmapped/code-section accesses exit back to the interpreter), calls into
the existing COP0/MXU implementations, and branch exits with inlined delay
slots. JR/JALR, syscalls, GOT dispatch, and anything unmapped still run on the
interpreter, so output is bit-identical (same frames, same syscall profile —
verified across all 28 bundled titles). `--jit=off` (default) keeps the pure
interpreter as the reference. Design and phase history live in
[docs/DYNAREC_PLAN.md](docs/DYNAREC_PLAN.md).

---

## Features & Implementation Status

CPU, display, audio, syscall coverage, app compatibility, and internals are documented in **[docs/FEATURES.md](docs/FEATURES.md)**.

## Supported games

Native 320×240 framebuffer captures (first / mid / last rendered frame) from [`docs/screenshots/`](docs/screenshots/). Compatibility notes are in [docs/FEATURES.md](docs/FEATURES.md#app-compatibility).

The emulator reports English as the firmware language. Games that can switch locales (for example **7 Days Salvation**) therefore start in English. Many other titles shipped only in Chinese and were never officially localized.

**Zhao Yun Chuan** (赵云传, *Legend of Zhao Yun*) is a 16-bit-style RPG with a large amount of dialogue. The original release is Chinese-only. The English build here is an unofficial localization: in-game dialogue and inscriptions were translated with AI tools so the story is readable.

### 7days

| 1 | 2 | 3 |
|:---:|:---:|:---:|
| ![7days 1](docs/screenshots/7days-1.png) | ![7days 2](docs/screenshots/7days-2.png) | ![7days 3](docs/screenshots/7days-3.png) |

### AliBaba

| 1 | 2 | 3 |
|:---:|:---:|:---:|
| ![AliBaba 1](docs/screenshots/alibaba-1.png) | ![AliBaba 2](docs/screenshots/alibaba-2.png) | ![AliBaba 3](docs/screenshots/alibaba-3.png) |

### Block Breaker

| 1 | 2 | 3 |
|:---:|:---:|:---:|
| ![Block Breaker 1](docs/screenshots/block-breaker-1.png) | ![Block Breaker 2](docs/screenshots/block-breaker-2.png) | ![Block Breaker 3](docs/screenshots/block-breaker-3.png) |

### Candy

| 1 | 2 | 3 |
|:---:|:---:|:---:|
| ![Candy 1](docs/screenshots/candy-1.png) | ![Candy 2](docs/screenshots/candy-2.png) | ![Candy 3](docs/screenshots/candy-3.png) |

### Decollation Warrior

| 1 | 2 | 3 |
|:---:|:---:|:---:|
| ![Decollation Warrior 1](docs/screenshots/decollation-warrior-1.png) | ![Decollation Warrior 2](docs/screenshots/decollation-warrior-2.png) | ![Decollation Warrior 3](docs/screenshots/decollation-warrior-3.png) |

### Formula-One

| 1 | 2 | 3 |
|:---:|:---:|:---:|
| ![Formula-One 1](docs/screenshots/formula-one-1.png) | ![Formula-One 2](docs/screenshots/formula-one-2.png) | ![Formula-One 3](docs/screenshots/formula-one-3.png) |

### Hell Striker II

| 1 | 2 | 3 |
|:---:|:---:|:---:|
| ![Hell Striker II 1](docs/screenshots/hell-striker-ii-1.png) | ![Hell Striker II 2](docs/screenshots/hell-striker-ii-2.png) | ![Hell Striker II 3](docs/screenshots/hell-striker-ii-3.png) |

### Landlord

| 1 | 2 | 3 |
|:---:|:---:|:---:|
| ![Landlord 1](docs/screenshots/landlord-1.png) | ![Landlord 2](docs/screenshots/landlord-2.png) | ![Landlord 3](docs/screenshots/landlord-3.png) |

### Link'em Up

| 1 | 2 | 3 |
|:---:|:---:|:---:|
| ![Link'em Up 1](docs/screenshots/linkem-up-1.png) | ![Link'em Up 2](docs/screenshots/linkem-up-2.png) | ![Link'em Up 3](docs/screenshots/linkem-up-3.png) |

### Manic-Miner

| 1 | 2 | 3 |
|:---:|:---:|:---:|
| ![Manic-Miner 1](docs/screenshots/manic-miner-1.png) | ![Manic-Miner 2](docs/screenshots/manic-miner-2.png) | ![Manic-Miner 3](docs/screenshots/manic-miner-3.png) |

### Mushroom Roulette

| 1 | 2 | 3 |
|:---:|:---:|:---:|
| ![Mushroom Roulette 1](docs/screenshots/mushroom-roulette-1.png) | ![Mushroom Roulette 2](docs/screenshots/mushroom-roulette-2.png) | ![Mushroom Roulette 3](docs/screenshots/mushroom-roulette-3.png) |

### Nose Breaker

| 1 | 2 | 3 |
|:---:|:---:|:---:|
| ![Nose Breaker 1](docs/screenshots/nose-breaker-1.png) | ![Nose Breaker 2](docs/screenshots/nose-breaker-2.png) | ![Nose Breaker 3](docs/screenshots/nose-breaker-3.png) |

### Platinum Sudoku

| 1 | 2 | 3 |
|:---:|:---:|:---:|
| ![Platinum Sudoku 1](docs/screenshots/platinum-sudoku-1.png) | ![Platinum Sudoku 2](docs/screenshots/platinum-sudoku-2.png) | ![Platinum Sudoku 3](docs/screenshots/platinum-sudoku-3.png) |

### Puzzle Bobble - PoPo Bash

| 1 | 2 | 3 |
|:---:|:---:|:---:|
| ![PoPo Bash 1](docs/screenshots/popo-bash-1.png) | ![PoPo Bash 2](docs/screenshots/popo-bash-2.png) | ![PoPo Bash 3](docs/screenshots/popo-bash-3.png) |

### Rick-Dangerous

| 1 | 2 | 3 |
|:---:|:---:|:---:|
| ![Rick-Dangerous 1](docs/screenshots/rick-dangerous-1.png) | ![Rick-Dangerous 2](docs/screenshots/rick-dangerous-2.png) | ![Rick-Dangerous 3](docs/screenshots/rick-dangerous-3.png) |

### Rubido

| 1 | 2 | 3 |
|:---:|:---:|:---:|
| ![Rubido 1](docs/screenshots/rubido-1.png) | ![Rubido 2](docs/screenshots/rubido-2.png) | ![Rubido 3](docs/screenshots/rubido-3.png) |

### snake

| 1 | 2 | 3 |
|:---:|:---:|:---:|
| ![snake 1](docs/screenshots/snake-1.png) | ![snake 2](docs/screenshots/snake-2.png) | ![snake 3](docs/screenshots/snake-3.png) |

### tetris

| 1 | 2 | 3 |
|:---:|:---:|:---:|
| ![tetris 1](docs/screenshots/tetris-1.png) | ![tetris 2](docs/screenshots/tetris-2.png) | ![tetris 3](docs/screenshots/tetris-3.png) |

### ultimate_drift

| 1 | 2 | 3 |
|:---:|:---:|:---:|
| ![ultimate_drift 1](docs/screenshots/ultimate-drift-1.png) | ![ultimate_drift 2](docs/screenshots/ultimate-drift-2.png) | ![ultimate_drift 3](docs/screenshots/ultimate-drift-3.png) |

### Yi-Chi King Fighter (Overlord Fighter)

| 1 | 2 | 3 |
|:---:|:---:|:---:|
| ![Yi-Chi King Fighter 1](docs/screenshots/yi-chi-king-fighter-1.png) | ![Yi-Chi King Fighter 2](docs/screenshots/yi-chi-king-fighter-2.png) | ![Yi-Chi King Fighter 3](docs/screenshots/yi-chi-king-fighter-3.png) |

### Zhao Yun Chuan (Chinese)

| 1 | 2 | 3 |
|:---:|:---:|:---:|
| ![Zhao Yun Chuan (Chinese) 1](docs/screenshots/zhao-yun-chuan-1.png) | ![Zhao Yun Chuan (Chinese) 2](docs/screenshots/zhao-yun-chuan-2.png) | ![Zhao Yun Chuan (Chinese) 3](docs/screenshots/zhao-yun-chuan-3.png) |

### Zhao Yun Chuan (English — Legend of Zhao Yun)

| 1 | 2 | 3 |
|:---:|:---:|:---:|
| ![Zhao Yun Chuan (English) 1](docs/screenshots/zhao-yun-chuan-en-1.png) | ![Zhao Yun Chuan (English) 2](docs/screenshots/zhao-yun-chuan-en-2.png) | ![Zhao Yun Chuan (English) 3](docs/screenshots/zhao-yun-chuan-en-3.png) |

## Performance

- Interpreter: ~35–50 million guest MIPS instructions / second on modern x86
- JIT (`--jit=on`): ~200 million guest insns / second on large titles
  (~4× the interpreter; 98% of insns run inside cached TBs on 7days)
- Typically 2M instructions per CPU frame, ~50–60 CPU frames for 1 rendered frame
- Runs approximately 5× slower than real JZ4730 hardware (360 MHz)
- Audio handled via lock-free ring + SDL callback (~20–32 ms fragments)

---
