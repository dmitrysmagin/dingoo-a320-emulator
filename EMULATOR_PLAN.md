# Emulator Plan for 7days.app (Dingoo A320 → Windows)

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│                    Windows Host (SDL2 + Win32)                      │
│                                                                     │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │              Emulator Core (MIPS32 Interpreter)              │   │
│  │                                                              │   │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌────────────┐  │   │
│  │  │ MIPS32   │  │ MXU      │  │ Memory   │  │ COP0       │  │   │
│  │  │ Decoder  │  │ (COP2)   │  │ Manager  │  │ (cache,    │  │   │
│  │  │ & Exec   │  │ Decoder  │  │ (MMU-less│  │  TLB stub) │  │   │
│  │  │          │  │ & Exec   │  │  KSEG0/1)│  │            │  │   │
│  │  └──────────┘  └──────────┘  └──────────┘  └────────────┘  │   │
│  │                                                              │   │
│  │  ┌──────────────────────────────────────────────────────┐   │   │
│  │  │            Dingoo OS Syscall Interception             │   │   │
│  │  │  (72 high-level function implementations via SDL/host)│   │   │
│  │  └──────────────────────────────────────────────────────┘   │   │
│  │                                                              │   │
│  │  ┌──────────────────────────────────────────────────────┐   │   │
│  │  │           Resource Archive Provider (7days.app)       │   │   │
│  │  │  Loads .spk from embedded data at 7days.app[0x150000] │   │   │
│  │  └──────────────────────────────────────────────────────┘   │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                     │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────────────┐   │
│  │ SDL2     │  │ SDL2     │  │ SDL2     │  │ Host FS          │   │
│  │ Video    │  │ Audio    │  │ Events   │  │ (save files)     │   │
│  │ (window) │  │ (PCM out)│  │ (keyboard)│  │                  │   │
│  └──────────┘  └──────────┘  └──────────┘  └──────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 1. MIPS32 Interpreter

### Core Specification

| Feature | Requirement | Notes |
|---------|-------------|-------|
| **ISA** | MIPS32 Release 1 (r1) | Dingoo JZ4730 core: MIPS32 4Kc-like, little-endian |
| **Endianness** | Little-endian (MIPSEB flag=0) | Confirmed by first 128 bytes parsing |
| **Delay slots** | Full support | Every branch/jump has a delay slot |
| **COP0** | Stub (17 MFC0, 2 MTC0) | Reads return 0, writes ignored. Count/TLB regs simulated minimally. |
| **COP1 (FPU)** | None | 0 real COP1 instructions in code section (false positives in data) |
| **COP2 (MXU)** | **Required** (30 instructions) | See Section 2 |
| **TLB** | Stub (8 instructions: TLBP, TLBWI, TLBWR) | Return success, don't modify TLB. Memory is flat-mapped. |
| **CACHE** | No-op | 0 CACHE instructions found anyway |
| **ERET** | No-op | 0 found, but should trigger a minimal exception return |

### Instruction Decode & Execute Loop

```
while (running):
    phys_pc = kseg1_to_phys(pc)         // strip 0xA0000000 or 0x80000000
    insn = read32(memory, phys_pc)       // fetch
    pc += 4                              // (will be adjusted for branches)

    opcode = insn >> 26
    switch (opcode):
        0x00:  decode_special(insn)      // SPECIAL opcode table
        0x01:  decode_regimm(insn)       // REGIMM opcode table
        0x02:  exec_j(insn)              // J
        0x03:  exec_jal(insn)            // JAL
        0x04:  exec_beq(insn)            // BEQ
        0x05:  exec_bne(insn)            // BNE
        0x06:  exec_blez(insn)           // BLEZ
        0x07:  exec_bgtz(insn)           // BGTZ
        0x08:  exec_addi(insn)           // ADDI
        0x09:  exec_addiu(insn)          // ADDIU
        0x0A:  exec_slti(insn)           // SLTI
        0x0B:  exec_sltiu(insn)          // SLTIU
        0x0C:  exec_andi(insn)           // ANDI
        0x0D:  exec_ori(insn)            // ORI
        0x0E:  exec_xori(insn)           // XORI
        0x0F:  exec_lui(insn)            // LUI
        0x10:  exec_cop0(insn)           // COP0 (stub)
        0x11:  exec_cop1(insn)           // COP1 (unimplemented → SIGFPE)
        0x12:  exec_cop2(insn)           // COP2 = MXU (implemented)
        0x13:  exec_cop3(insn)           // COP3 (unimplemented → SIGFPE)
        0x14:  exec_beql(insn)           // BEQL
        0x15:  exec_bnel(insn)           // BNEL
        0x16:  exec_blezl(insn)          // BLEZL
        0x17:  exec_bgtzl(insn)          // BGTZL
        0x18..0x1F: exec_load_store(insn)  // LB/LH/LWL/LW/LBU/LHU/LWR
        0x20..0x26: exec_load(insn)        // LB/LH/LWL/LW/LBU/LHU/LWR
        0x27..0x2E: exec_store(insn)       // SB/SH/SWL/SW/SWR
        0x2F..0x31: exec_load_store(insn)  // CACHE/LL/LWC1/LWC2
        0x32..0x3E: exec_store(insn)       // SWC1/SWC2/SC/SDC1/SWC3
        0x3F:  exec_special2(insn)      // SPECIAL2 (MUL/MADD/MSUB/etc.)

    if exception_raised:
        handle_exception()
    if branch_taken:
        pc = branch_target
        execute_delay_slot = false  // already executed
    // sync check/resource check here
```

### Memory Access (Load/Store)

All loads/stores go through a single function that:
1. Translates virtual address to physical (simple: `KSEG0_MASK = ~0x9FFFFFFF | ~0x80000000` → strip bit 29 for KSEG0→KSEG1, or just mask off top bits)
2. Checks for unmapped regions → bus error exception
3. Reads/writes the memory array

```
phys = mips_vaddr_to_phys(vaddr):
    if (vaddr & 0x80000000) == 0:
        // usermode address - on Dingoo this is unmapped
        raise exception(EXC_ADEL)
    if (vaddr & 0xE0000000) == 0x80000000:
        // KSEG0 or KSEG1: strip bit 29
        return vaddr & 0x1FFFFFFF
    // KSEG2/3 or other: unmapped
    raise exception(EXC_ADEL)

Memory read/write goes through bounds checking:
    if phys < MEMORY_SIZE:
        mem[phys] = value
    else:
        // Hardware register range - return 0, ignore writes
        if read: return 0
        if write: discard
```

### Registers

```
struct CPU {
    uint32_t regs[32];        // GPRs: $zero..$ra
    uint32_t pc;              // Program counter
    uint32_t hi, lo;          // Multiply/divide unit
    uint32_t llbit;           // Load-linked / Store-conditional

    // COP0 registers (minimal)
    struct {
        uint32_t status;      // SR: interrupt mask, enable bits
        uint32_t cause;       // CAUSE: exception type, pending IRQs
        uint32_t epc;         // Exception PC
        uint32_t badvaddr;    // Bad virtual address
        uint32_t count;       // Timer count
        uint32_t compare;     // Timer compare
        uint32_t prid;        // Processor ID (0x00018200 for 4Kc)
        uint32_t config;      // Configuration
        // TLB entries - not strictly needed but having nop stubs simplifies
    } cp0;

    // MXU (COP2) registers
    struct {
        // 16 accumulator registers (MXU regs are 32-bit?)
        // MXU has: 16 X registers, accumulator, control regs
        uint32_t acc[16];     // MXU accumulator registers
        uint32_t ctrl[16];    // MXU control registers 
    } mxu;
};
```

---

## 2. MXU Coprocessor (COP2) — Required Subset

### Instructions Found

| Type | Count | MXU Op | Likely Operation |
|------|-------|--------|-----------------|
| **MFC2** | 6 | — | Move MXU reg → CPU reg |
| **CFC2** | 7 | — | Move MXU control reg → CPU reg |
| **MTC2** | 2 | — | Move CPU reg → MXU reg |
| **Custom** | 2 | `0x1` | DSP multiply-accumulate step? |
| **Custom** | 4 | `0x3` | Multiply-add (MAD) variant |
| **Custom** | 1 | `0x8` | Shift/align |
| **Custom** | 1 | `0x9` | Shift/align variant |
| **Custom** | 2 | `0xB` | Compare/select |
| **Custom** | 1 | `0x11` | Multiply |
| **Custom** | 1 | `0x14` | MAC (multiply-accumulate) |
| **Custom** | 1 | `0x19` | Divide step? |
| **Custom** | 1 | `0x1B` | DSP accumulate |
| **Custom** | 1 | `0x1E` | DSP finalize |

### Implementation Strategy

The JZ4730 MXU is a DSP coprocessor with 16 accumulator registers and various SIMD/MAC operations. Rather than implementing every MXU instruction perfectly, the emulator can:

1. **Log unknown MXU ops** with register values on first encounter
2. **Evaluate the actual operations** from context — all MXU instructions are clustered in a few code regions
3. **Implement only observed MXU ops** (which can be determined by tracing)

The MXU appears to be used for:
- Audio mixing (multiple .sau channels → PCM output)
- Fixed-point math for 3D transforms
- Color conversion/blending in graphics

**Register mapping** (MXU in JZ4730):
```
Registers:
  MXU_ACC[0..15]    — main accumulator array (32-bit each)
  MXU_CTRL[0..15]   — control registers
  
Key custom operations to decode:
  Op 0x01: MXU_OP_MADDR  — Multiply, add, double, round
  Op 0x03: MXU_OP_MAD    — Multiply-add
  Op 0x08: MXU_OP_SLINS  — Shift left and insert
  Op 0x09: MXU_OP_SRINS  — Shift right and insert
  Op 0x0B: MXU_OP_CPS     — Compare and select
  Op 0x11: MXU_OP_MUL     — Multiply
  Op 0x14: MXU_OP_MAC     — Multiply-accumulate
  Op 0x19: MXU_OP_DIV     — Divide step
  Op 0x1B: MXU_OP_ACC     — Accumulate
  Op 0x1E: MXU_OP_FIN     — Finalize/round
```

---

## 3. Memory Layout

### Physical Memory Map

```
Address            | Size    | Content
-------------------|---------|--------------------------------
0x00000000-0x00000FFF | 4 KB  | Boot area (unused at runtime)
0x00100000-0x00FFFFFF | ~15 MB | Reserved / unmapped
0x01000000-0x01FFFFFF | 16 MB | Possible heap/malloc arena
0x02000000-0x03FFFFFF | 32 MB | Reserved
0x04000000-0x07FFFFFF | 64 MB | NAND flash cache area
0x08000000-0x1FFFFFFF | 384 MB | Unmapped

KSEG0/KSEG1 aliases (emulated as same memory):
  KSEG0: 0x80000000-0x9FFFFFFF → phys 0x00000000-0x1FFFFFFF
  KSEG1: 0xA0000000-0xBFFFFFFF → phys 0x00000000-0x1FFFFFFF
```

### Memory Regions for Emulation

```
Region              | Start        | End          | Size    | Notes
--------------------|--------------|--------------|---------|--------------------
Code + Data (RAWD)  | 0x80A00000   | 0x80B44270   | 1.3 MB  | Loaded from 7days.app[0x970]
BSS (zeroed)        | 0x80B44270   | 0x80B45000   | 3.5 KB  | Actually extends to 0x80B44270+? depends on prog_size
Heap area           | 0x80B45000   | 0x80C00000   | ~1 MB   | malloc arena
Stack               | 0x80C00000   | 0x80C10000   | 64 KB   | Stack grows down from high addr
Save data area      | 0x80C10000   | 0x80C20000   | 64 KB   | Mirrors state.sdt / config.sdt
Resource cache      | 0x80C20000   | 0x80E00000   | 2 MB    | Cache for loaded resources
Extra RAM           | 0x80000000   | 0x81000000   | 16 MB   | Full RAM if needed

Total emulated RAM: 16 MB (same as Dingoo A320 hardware)
```

### Memory Manager Responsibilities

```
init_memory():
    mem = malloc(16 * 1024 * 1024)
    memset(mem, 0, size)
    
    // Load binary code+data
    rawd = 7days.app[0x0970 : 0x0970 + 0x139CE0]
    memcpy(mem + 0x00A00000, rawd, len(rawd))
    
    // Zero BSS
    memset(mem + 0x00B44270, 0, bss_size)

translate(vaddr):
    if (vaddr & 0x80000000) == 0:
        return PHYS_UNMAPPED  // user addresses not used
    if (vaddr & 0xE0000000) == 0x80000000:
        return vaddr & 0x1FFFFFFF  // KSEG0/KSEG1 → same phys
    return PHYS_UNMAPPED

read_byte(vaddr):
    phys = translate(vaddr)
    if phys is unmapped: raise EXC_ADEL
    if phys >= HW_BASE: return 0  // HW reads return 0
    return mem[phys]
```

---

## 4. Dingoo OS Syscall Interception

### Architecture

The MIPS32 binary calls Dingoo OS functions via JAL to GOT/trampoline entries. The GOT entries at 0x80AD67E0–0x80AD6A1C are normally patched by the Dingoo OS loader to jump to OS service routines.

**Emulation approach**: Pre-populate the GOT trampolines so each `jal thunk_addr` is intercepted by the emulator, which then dispatches to the corresponding C function implementation.

### Implementation: GOT Dispatch Table

```
// GOT trampoline addresses and their handlers
struct GOTEntry {
    uint32_t    addr;       // trampoline address (0x80AD67E0 + i*8)
    const char* name;       // function name from IMPT
    void*       handler;    // C function pointer
};

GOTEntry got_table[72] = {
    {0x80AD67E0, "abort",                  impl_abort},
    {0x80AD67E8, "printf",                 impl_printf},
    {0x80AD67F0, "sprintf",                impl_sprintf},
    {0x80AD67F8, "fprintf",                impl_fprintf},
    {0x80AD6800, "strncasecmp",            impl_strncasecmp},
    {0x80AD6808, "malloc",                 impl_malloc},
    {0x80AD6810, "realloc",                impl_realloc},
    {0x80AD6818, "free",                   impl_free},
    {0x80AD6820, "fread",                  impl_fread},
    // ... all 72 entries ...
};
```

### Dispatch Mechanism

Option A — **Dynamic recompilation of GOT slots**:
```
on_GOT_fetch(address):
    handler = got_table[address_to_index(addr)]
    // Replace the 8-byte trampoline slot with a direct jump
    // This requires self-modifying code support
```

Option B — **Trampoline injection**:
```
Instead of JAL to GOT, patch at runtime:
  When a JAL targets a GOT address, look up handler and call it directly.
  No self-modifying code needed — just check in the fetch/decode stage.
```

Option C — **Memory protection + signal handler** (simplest):
```
Mark GOT pages as no-execute.
When the CPU tries to execute the crash instruction,
the interpreter catches it and routes to the handler.
```

### Function Implementation Groups

#### 4a. Memory / libc stubs

```c
// These manage the emulator's internal heap
void* impl_malloc(uint32_t size) {
    return emu_heap_alloc(size);
}
void impl_free(void* ptr) {
    emu_heap_free(ptr);
}
void* impl_realloc(void* ptr, uint32_t size) {
    return emu_heap_realloc(ptr, size);
}
int impl_printf(const char* fmt, ...) {
    // Forward to host stdout with logging prefix
    return emu_log(LOG_INFO, fmt, args);
}
int impl_strncasecmp(const char* a, const char* b, uint32_t n) {
    return _strnicmp(a, b, n);
}
```

#### 4b. Display (SDL2 Video)

```c
// These control the LCD framebuffer
// Original Dingoo: 320×240 LCD at 16bpp (RGB565)

static uint16_t framebuffer[320 * 240];  // emulated LCD framebuffer
static SDL_Window* window;
static SDL_Renderer* renderer;
static SDL_Texture* texture;

void impl_lcd_flip() {
    // Dingoo lcd_flip swaps front/back buffers
    SDL_UpdateTexture(texture, NULL, framebuffer, 320 * 2);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}

void impl__lcd_set_frame(uint32_t addr) {
    // Dingoo: set the LCD controller to read from this RAM address
    // Emulator: just save the address, use it for next flip
    current_frame_addr = addr;
}

uint32_t impl_LcdGetDisMode() {
    // Return current display mode (0=off, 1=on)
    return 1;
}
```

#### 4c. Audio (SDL2 Audio)

```c
// The Dingoo audio system uses a waveout API that writes PCM samples
// Format: 16-bit signed, 44100 Hz, stereo

static SDL_AudioDeviceID audio_dev;
static std::queue<int16_t> audio_queue;
static SDL_mutex* audio_mutex;

void waveout_callback(void* userdata, Uint8* stream, int len) {
    SDL_LockMutex(audio_mutex);
    int samples = len / 2;
    for (int i = 0; i < samples && !audio_queue.empty(); i++) {
        ((int16_t*)stream)[i] = audio_queue.front();
        audio_queue.pop();
    }
    SDL_UnlockMutex(audio_mutex);
}

int impl_waveout_open(int sample_rate, int channels, int bits) {
    SDL_AudioSpec want, have;
    want.freq = sample_rate;
    want.format = AUDIO_S16LSB;
    want.channels = channels;
    want.samples = 1024;
    want.callback = waveout_callback;
    audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    SDL_PauseAudioDevice(audio_dev, 0);
    return (audio_dev > 0) ? 0 : -1;
}

int impl_waveout_write(uint32_t buf_addr, uint32_t size) {
    // buf_addr is a MIPS guest address
    int16_t* samples = (int16_t*)guest_to_host(buf_addr);
    int count = size / 2;
    SDL_LockMutex(audio_mutex);
    for (int i = 0; i < count; i++)
        audio_queue.push(samples[i]);
    SDL_UnlockMutex(audio_mutex);
    return size;
}
```

#### 4d. Input (SDL2 Events)

```c
// Dingoo keyboard has:
//   A, B, X, Y, L, R, START, SELECT, UP, DOWN, LEFT, RIGHT
// Plus Dingoo-specific: VOLUME_UP, VOLUME_DOWN, etc.

static uint32_t kbd_status;  // bitmask of pressed keys

int impl__kbd_get_key() {
    // Return next key press from queue
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
            uint32_t d_key = sdl_to_dingoo_key(event.key.keysym.sym);
            if (event.type == SDL_KEYDOWN)
                kbd_status |= d_key;
            else
                kbd_status &= ~d_key;
            return d_key;  // implementation detail depends on Dingoo API
        }
    }
    return 0;
}

uint32_t impl__kbd_get_status() {
    return kbd_status;
}
```

#### 4e. Filesystem (Host FS + Embedded Archive)

```c
// The Dingoo filesystem provides fopen/fread/fseek/fclose
// for two types of files:
//   1. Embedded resources (read-only, from 7days.app archive)
//   2. Save files (read-write, on host filesystem)

struct FileHandle {
    bool is_embedded;
    FILE* host_file;          // for save files
    uint8_t* embedded_data;   // for archive files
    uint32_t embedded_size;
    uint32_t offset;
    uint32_t error;
    bool eof;
};

// Pre-load the resource archive
static uint8_t* resource_archive = NULL;
static uint32_t resource_size = 0;
static SPKArchive* archives[16];  // opened .spk files

void init_resource_system() {
    // Load resource section from 7days.app[0x150000:]
    resource_archive = load_file("7days.app");
    resource_size = file_size - 0x150000;
    resource_archive_ptr = resource_archive + 0x150000;
}

FileHandle* impl_fsys_fopen(const char* path, const char* mode) {
    // Check if this is a save file (writable) or resource (read-only)
    if (is_save_file(path)) {
        // Open real file in emulator's save directory
        FILE* f = fopen(make_host_path(path), mode);
        return create_handle(f, false);
    }
    
    // Resource files: look in pre-loaded archive
    // Path normalization: ".\common\common.spk" → "common.spk"
    char* normalized = normalize_path(path);
    uint8_t* data = find_in_archive(resource_archive_ptr, normalized);
    if (data) {
        return create_handle(data, file_size, true);
    }
    
    return NULL;  // file not found
}

// Save files go to host filesystem
static const char* SAVE_FILES[] = {
    ".\\common\\state.sdt",
    ".\\common\\str.sdt",
    ".\\config.sdt",
    ".\\common\\itemdata.sdt",
    // Also: slot%d.sav, photo_%02d, etc.
};
```

#### 4f. µC/OS-II RTOS (Stubs)

```c
// The game uses basic RTOS primitives.
// In the emulator, these can be simplified:

int impl_OSTimeGet() {
    return SDL_GetTicks();  // ms since start
}

void impl_OSTimeDly(int ticks) {
    SDL_Delay(ticks);  // ms sleep (1 tick ≈ 1 ms in Dingoo OS)
}

int impl_OSSemCreate(int initial_count) {
    return emu_sem_create(initial_count);
}

int impl_OSSemPend(int sem_id, int timeout) {
    return emu_sem_wait(sem_id, timeout);
}

int impl_OSSemPost(int sem_id) {
    return emu_sem_signal(sem_id);
}

int impl_OSSemDel(int sem_id) {
    return emu_sem_destroy(sem_id);
}

// Task creation — the game creates tasks, but in a single-threaded
// emulator we can simply run them cooperatively or sequentially
int impl_OSTaskCreate(uint32_t entry, uint32_t arg, ...) {
    emu_task_register(entry, arg, priority);
    return 0;
}
```

#### 4g. Timer

```c
uint32_t impl_StartSwTimer(uint32_t interval_ms, uint32_t callback) {
    return emu_timer_create(interval_ms, callback);
}

void impl_free_irq(int irq_num) {
    // Stub — free interrupt handler
}
```

#### 4h. Cache Management (No-ops)

```c
void impl___icache_invalidate_all() {
    // No-op on emulator
}
void impl___dcache_writeback_all() {
    // No-op on emulator
}
```

#### 4i. Serial (Logging)

```c
// The game uses serial_putc for debug output
void impl_serial_putc(char c) {
    putchar(c);  // or log to file
}

char impl_serial_getc() {
    return 0;  // no serial input
}
```

---

## 5. Resource Self-Access Handling

### Problem

The 7days.app executable contains its own resource archive starting at file offset `0x150000`. On real Dingoo hardware, the OS loader extracts or maps this section, and `fsys_fopen` calls redirect to it.

In the emulator, the MIPS binary will call `fsys_fopen("common.spk")` and expect to get a file handle pointing to the embedded `.spk` data.

### Solution

```
 ┌───────────────────────────────────────────────────────┐
 │                 7days.app                             │
 │                                                       │
 │  ┌─────┬──────┬──────┬──────────────────────────────┐ │
 │  │CCDL │IMPT  │EXPT  │           RAWD              │ │
 │  │     │      │      │  (MIPS32 code + data)        │ │
 │  └─────┴──────┴──────┴──────────────────────────────┘ │
 │                                                       │
 │  ┌──────────────────────────────────────────────────┐ │
 │  │           Resource Section (0x150000+)           │ │
 │  │  ┌───────────┐  ┌───────────┐  ┌──────────────┐ │ │
 │  │  │common.spk │  │ day1.spk  │  │ audio/       │ │ │
 │  │  │  (TOC)    │  │  (TOC)    │  │  a_getitem   │ │ │
 │  │  │  0101.spl │  │  01.sst   │  │  c_biddy_die │ │ │
 │  │  │  1000.spl │  │  02.sst   │  │  ...         │ │ │
 │  │  │  ...      │  │  00.sbp   │  │              │ │ │
 │  │  │           │  │  ...      │  │              │ │ │
 │  │  └───────────┘  └───────────┘  └──────────────┘ │ │
 │  └──────────────────────────────────────────────────┘ │
 └───────────────────────────────────────────────────────┘
         │                              
         │ Pre-load at emulator start   
         ▼
 ┌──────────────────────────────────────────────┐
 │         Resource Provider                    │
 │                                              │
 │  • Parse .spk TOC at startup                 │
 │  • Build hash table: name → (data, size)     │
 │  • fsys_fopen → lookup in hash table         │
 │  • fsys_fread → memcpy from hash table       │
 │  • fsys_fseek → adjust offset in handle      │
 │  • fsys_fclose → free handle                 │
 └──────────────────────────────────────────────┘
```

### Implementation

```c
struct SPKArchive {
    struct Entry {
        std::string name;
        uint32_t    offset;
        uint32_t    size;
        uint8_t*    data;   // pointer into the loaded archive
    };
    std::vector<Entry> entries;
    std::unordered_map<std::string, Entry*> name_map;
    uint8_t* raw_data;  // the loaded .spk file data
};

// During init:
// 1. Load the entire resource section from 7days.app[0x150000:]
// 2. Walk the .spk TOC (it's a single flat archive with 3216 entries)
// 3. Build the name→data lookup table
// 4. For each fsys_fopen call, look up the normalized path

const char* normalize_path(const char* dingoo_path) {
    // ".\audio\a_getitem.sau" → "audio/a_getitem.sau"
    // ".\common\common.spk"   → "common.spk"
    if (dingoo_path[0] == '.' && dingoo_path[1] == '\\')
        dingoo_path += 2;
    // Convert backslash to forward slash
    static char buf[256];
    strcpy(buf, dingoo_path);
    for (char* p = buf; *p; p++)
        if (*p == '\\') *p = '/';
    return buf;
}

// The .spk archive entries use paths like ".\audio\a_getitem.sau"
// Normalize before lookup
```

---

## 6. Emulation Loop & Synchronization

### Main Loop

```
void emulator_run() {
    init_sdl();
    init_memory();      // Load RAWD, zero BSS
    init_handlers();    // Register all 72 syscall handlers
    init_resources();   // Pre-load 7days.app resource section
    init_got_table();   // Set up GOT dispatch
    
    // Set initial PC to dl_main (0x80AD6A20)
    cpu.pc = 0x80AD6A20;
    cpu.regs[29] = STACK_INIT;  // $sp = stack top
    cpu.regs[30] = STACK_INIT;  // $fp = stack top
    cpu.regs[4] = 0;            // $a0 = 0 (init mode)
    cpu.regs[5] = 0;            // $a1 = 0 (cold boot)
    
    uint32_t last_time = SDL_GetTicks();
    uint32_t frame_count = 0;
    
    while (!quit_requested) {
        // Process SDL events (input, quit, window)
        pump_sdl_events();
        
        // Execute one "frame" of MIPS instructions
        // The Dingoo runs at ~360 MHz. We execute in bursts.
        // Target: ~16ms per frame (60 fps) or ~33ms (30 fps)
        uint32_t frame_start = SDL_GetTicks();
        uint32_t insn_count = 0;
        uint32_t max_insns = 100000;  // tune this
        
        while (insn_count < max_insns && !pending_vsync) {
            execute_one_instruction();
            insn_count++;
            
            // Check every N instructions if audio buffer needs filling
            if (insn_count % 1000 == 0) {
                service_audio();  // Push samples if queue is low
                service_timers(); // Fire any pending OS timers
            }
        }
        
        // Sync to host frame rate
        uint32_t frame_time = SDL_GetTicks() - frame_start;
        if (frame_time < 16) {
            SDL_Delay(16 - frame_time);  // cap at 60fps
        }
        
        // Render if lcd_flip was called
        if (frame_dirty) {
            present_frame();
            frame_dirty = false;
        }
        
        frame_count++;
    }
}
```

### Instruction Budget

| CPU | Clock | Insns/cycle | Target insns/frame (60fps) |
|-----|-------|-------------|---------------------------|
| Real JZ4730 | 360 MHz | ~0.8 IPC (typical) | ~4,800,000 |
| Interpreter | — | effective | ~50,000–200,000 (tunable) |

Real-time synchronization is achieved by:
1. Executing a batch of instructions per frame
2. Sleeping any remaining time to hit 60fps
3. Audio callbacks drive the timing if audio is the bottleneck

### Syncing Strategy

```
Timer model: The Dingoo µC/OS-II tick is ~1ms.
- OSTimeGet() returns SDL_GetTicks()
- OSTimeDly(n) calls SDL_Delay(n)
- This keeps the game's sense of time in sync with wall clock

Frame pacing:
- The game renders frames at its own pace (calls lcd_flip when ready)
- The emulator presents each frame immediately on lcd_flip
- The V-sync is handled by SDL2's SwapInterval
- If the game runs faster than 60fps, we cap via SDL_Delay
- If slower, we let it run (no speedup — it's an interpreter, it'll be slow enough)
```

---

## 7. COP0 Stub Details

### Observed COP0 Operations

```
MFC0  $rt, $rd          (17×)  — Read system register
  Found register selects: Count, Compare, Status, Cause, PRId, Config
  Return simulated values.

MTC0  $rt, $rd          (2×)   — Write system register
  Writes to Count/Compare — need to track for timer interrupts.

TLBP                     (1×)  — TLB Probe (not needed, no actual TLB)
TLBWI                    (3×)  — TLB Write Indexed
TLBWR                    (4×)  — TLB Write Random
  All are boot-time initialization. Return without doing anything.
```

### Implementation

```c
void exec_cop0_mfc0(uint32_t insn) {
    int rd = (insn >> 11) & 0x1F;  // COP0 register select
    int rt = (insn >> 16) & 0x1F;  // CPU destination register
    
    switch (rd) {
    case 0:   // Index (for TLB)
    case 1:   // Random (for TLB)
    case 2:   // EntryLo0
    case 3:   // EntryLo1
    case 4:   // Context
    case 5:   // PageMask
    case 6:   // Wired
        cpu.regs[rt] = 0; break;
    case 9:   // Count
        cpu.regs[rt] = SDL_GetTicks(); break;
    case 11:  // Compare
        cpu.regs[rt] = cpu.cp0.compare; break;
    case 12:  // Status
        cpu.regs[rt] = cpu.cp0.status; break;
    case 13:  // Cause
        cpu.regs[rt] = cpu.cp0.cause; break;
    case 14:  // EPC
        cpu.regs[rt] = cpu.cp0.epc; break;
    case 15:  // PRId
        cpu.regs[rt] = 0x00018200; break;  // MIPS 4Kc
    case 16:  // Config
        cpu.regs[rt] = 0x8000; break;      // basic config
    default:
        cpu.regs[rt] = 0;
        log("Unknown MFC0 rd=%d", rd);
    }
}

void exec_cop0_tlbwi() {
    // No-op. TLB is unused at runtime.
}
```

---

## 8. Development Plan

### Phase 1: Minimal Viable Emulator — ✅ COMPLETE
- [x] MIPS32 interpreter (all standard opcodes, incl. LWL/LWR with correct LE formulas)
- [x] Memory management (KSEG0/KSEG1 flat map) — **128 MB allocated** (not 16 MB as planned; needed to hold resource archive in guest RAM at phys 0x00B50000)
- [x] COP0 stubs (Count/Compare auto-increment per instruction)
- [x] All 72 GOT handlers implemented
- [x] RAWD loaded at 0x80A00000, BSS zeroed, stack initialised
- [x] dl_main → AppMain executes; game reaches dialogue loop

### Phase 2: Display — ✅ COMPLETE (implementation significantly exceeded plan)
The plan assumed a single framebuffer pointed to by `_lcd_set_frame`. In reality the game
uses two separate buffers — background and text overlay — composited by the LCD hardware.

- [x] `_lcd_set_frame` / `_lcd_get_frame` / `LcdGetDisMode` implemented
- [x] SDL2 window: 320×240 internal, scaled 3× → 960×720
- [x] **Smart framebuffer scan**: samples heap at 0x4000 steps every 50 calls to find richest buffer
- [x] **Composite rendering** (`flip_composite`): background from scan winner, text from `g_detected_fb_addr`
- [x] CPU interception at PC 0x80A21E78 tracks render-target address
- [x] RGB565 format verified byte-compatible with SDL on LE host; no conversion needed in hot path
- [x] Screenshot conversion fixed: `SDL_ConvertSurfaceFormat` gives proper 5→8/6→8 bit expansion
- [x] Game renders full-screen backgrounds (99.6–100% pixel coverage) with advancing dialogue text

### Phase 3: Filesystem + Resources — ✅ MOSTLY COMPLETE
- [x] `fsys_fopenW` / `fsys_fread` / `fsys_fseek` / `fsys_ftell` / `fsys_fclose`
- [x] Resource archive loader (3216 SPK entries served on demand)
- [x] Resource archive also loaded into guest RAM at phys 0x00B50000
- [ ] **Save file write path** — `slot1-3.sav` / `config.sdt` return NOT FOUND; game handles gracefully but no persistence

### Phase 4: Input — ✅ COMPLETE
- [x] Full SDL keyboard → Dingoo bitmask mapping (`display.h`)
- [x] SDL event loop in `pump_events()` feeding `m_dingoo_keys`
- [x] `_sys_judge_event` reads event queue at 0x80BFECD8; live keypresses work
- [x] Auto-press schedule (START at vsync 200, then A every 100 vsyncs to vsync 3000) for automated testing
- [x] `SDL_VIDEODRIVER=offscreen` + software renderer fallback for headless runs
- Note: SELECT mapped to Tab (plan appendix said Right Shift — minor divergence)

### Phase 5: Audio — ❌ STUBBED, NOT IMPLEMENTED
The audio task runs correctly and produces PCM data; it is silently discarded.

- [x] `waveout_open` / `waveout_close` / `waveout_can_write` (stubs; audio task unblocked)
- [x] `waveout_write` called ~737K times per vsync-1000 — data arrives at correct rate
- [ ] **SDL audio device never opened** — no `SDL_OpenAudioDevice` call
- [ ] **PCM samples discarded** — `waveout_write` is a no-op returning size
- [ ] **SDL audio callback not implemented**
- Next step: open device in `waveout_open`, queue samples in `waveout_write`, drain in callback

### Phase 6: MXU Implementation — ✅ PRESENT, CORRECTNESS UNVERIFIED
- [x] `mxu.cpp` handles all COP2 custom opcodes encountered
- [x] Zero unknown-opcode hits across 6.4 billion instructions in a 2-minute run
- [ ] Output correctness unverified — audio is the main consumer but is not being played back
- Note: will need re-verification once Phase 5 is complete

### Phase 7: Polish — ⚠️ PARTIAL
- [ ] Frame rate capping (60fps) — currently uncapped; emulator is ~5× slower than real hardware so irrelevant for now
- [ ] Save/load state synchronisation — blocked on Phase 3 save write path
- [ ] Config file support (`config.sdt`) — game uses Chinese-language defaults without it
- [x] Window scaling (3× scale)
- [x] Debug logging (GOT call counts, LCD scan, frame stats, auto-press)
- [x] Screenshot auto-save at milestones (frames 1–5, every 25th)
- Performance: 64M insns/s (~5× slower than 336 MHz JZ4730); was 14–20M at Phase 1 completion

---

## 9. Key Risks & Mitigations

| Risk | Impact | Mitigation / Status |
|------|--------|---------------------|
| MXU instruction set incompletely understood | Audio/3D corruption | All observed ops implemented; no unknowns in 6.4B insns. Verify once audio plays back. |
| TLB operations during boot are complex | Boot hangs | ✅ TLBWI/WR confirmed no-ops at runtime |
| .spk path resolution differs from Dingoo OS | Resource loading fails | ✅ 177+ successful fread calls across 3216-entry archive |
| µC/OS-II task model (multithreading) | Wrong execution order | ✅ Preemptive time-slicing in simulate_vsync; both tasks get CPU time |
| Audio timing mismatch | Crackling/stuttering | SDL2 callback + queue still the right approach; not yet implemented |
| Performance of interpreter | Too slow for gameplay | 64M insns/s achieved; 5× slower than real HW; acceptable for dialogue game |

---

## Appendix A: Quick Reference

### Dingoo Key Mapping for SDL2

```
Dingoo  →  SDL2          →  PC Key
─────────────────────────────────────
UP         SDLK_UP          ↑
DOWN       SDLK_DOWN        ↓
LEFT       SDLK_LEFT        ←
RIGHT      SDLK_RIGHT       →
A          SDLK_z           Z
B          SDLK_x           X
X          SDLK_a           A
Y          SDLK_s           S
L          SDLK_q           Q
R          SDLK_w           W
START      SDLK_RETURN      Enter
SELECT     SDLK_RSHIFT      Right Shift
VOL+       SDLK_EQUALS      =
VOL-       SDLK_MINUS       -
```

### .spk Archive Layout (Dingoo)

```
Offset 0:    uint16 LE  →  entry count (3216)
Offset 2:    entries[]  →  for each entry:
                 char[64] name (0x00-padded, ".\dir\file.ext")
                 uint32 LE offset
Offset 2 + 3216×68 = 218,690:  → first file data
```

### Resource Section Access

```
7days.app:
  [0x000000..0x000FFF]  =  Header + IMPT + EXPT tables
  [0x000970..0x13A650]  =  MIPS32 RAWD (code + data)
  [0x13A651..0x14FFFF]  =  Padding
  [0x150000..EOF]       =  Resource archive (.spk TOC + file data)
```

### Key Memory Addresses

```
0x80A00000              =  RAWD load origin
0x80AD6A20              =  dl_main entry point
0x80AD67E0..0x80AD6A1C  =  GOT/import trampolines (72 × 8 B)
0x80AD6B1C              =  AppMain (game start)
0x80A003E8              =  GameEngine initialization
0x80B41CE0              =  BSS start (end of initialized data)
0x80B44270              =  BSS end / prog_size boundary
0x80B44270 + bss        =  Heap begins
```
