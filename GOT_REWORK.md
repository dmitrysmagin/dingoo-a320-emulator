# GOT Dispatch Rework: Name-Indexed Lookup

## Problem

The emulator has three GOT layout formats — no two games agree on which function lives at which GOT index:

| Format | Games | Imports | Dispatch |
|--------|-------|---------|----------|
| **Standard (72)** | tetris, snake, 7days, … | 72 | `switch(got_index)` |
| **Extended (96)** | Yi-Chi, Overlord | 96 | `m_got_remap[i]` → switch |
| **Mini (172)** | Life, StopWatch, dicer | 172 (many are name-only) | not dispatched |

The old fix (`m_got_remap` + `m_use_got_remap` flag) was a per-game hack that doesn't scale. The real Dingoo firmware resolves GOT entries by **name** via the import table, not by positional index.

## Solution: Name-Indexed Dispatch

### A. Static handler registry (`s_handlers[]`)

A single sorted table of all known API functions:

```cpp
struct GOTHandler {
    const char* name;                     // import name (canonical)
    void (Syscalls::*handler)();          // member function pointer
    bool is_stub;
};
```

Singleton table (115 entries), sorted alphabetically by `strcmp` for `bsearch`.

### B. Per-slot handler vector (`m_slot_handlers`)

One entry per import slot, populated at startup:

```
init:  for each import name → bsearch s_handlers[] → store index
run:   m_slot_handlers[got_index] → lookup → call handler
```

### C. Clean dispatch

```cpp
void Syscalls::dispatch(int got_index, u32 /*return_addr*/) {
    m_got_call_count++;
    int hi = m_slot_handlers[got_index];
    if (hi >= 0) {
        (this->*s_handlers[hi].handler)();
    } else {
        printf("[SYSCALL] Unknown GOT index %d\n", got_index);
    }
}
```

No `switch`, no `m_got_remap`, no per-game branches.

### D. Name resolution at init

`init_slot_handlers()` iterates the parsed import table, binary-searches `s_handlers[]`, and fills `m_slot_handlers` with indices. Unknown names get `-1`.

### E. What goes away

| Component | Reason |
|-----------|--------|
| `switch(got_index)` in dispatch | Replaced by table lookup |
| `m_got_remap` + `m_use_got_remap` | No per-game branches |
| `build_got_remap()` + `remap_got_index()` | Unnecessary |
| `set_got_remap()` | Unnecessary |
| `m_import_names` | Replaced by `m_slot_handlers` |
| `got_name_import()` | Unnecessary |
| Yi-Chi detection in `main.cpp` | Removed — same init for all apps |

### F. Aliases / name variants

Some functions have multiple import names across apps (e.g. `fopen` vs `fsys_fopen`). Each variant gets its own entry in `s_handlers[]` pointing to the same implementation. The user explicitly noted that `_kbd_get_status` and `kbd_get_status` are **different functions** in the real Dingoo OS (even if the emulator implements them identically), so they remain separate table entries.

### G. Per-function stub tracking

The `is_stub` flag lives in `GOTHandler`, not in hardcoded switch cases. Any function — regardless of which app imports it or at what GOT index — is correctly identified as stub or implemented.
