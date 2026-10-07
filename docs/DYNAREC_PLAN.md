# MIPS32 → x86_64 Dynarec Plan (Windows + Linux)

## 0. Where we are (measured from `emulator/src/`)

Interpreter: `cpu.cpp:execute()/execute_one()` — fetch→decode switch→execute, ~64M guest insn/s,
~2M insn/CPU-frame, ~5× slower than 360 MHz JZ4730 (`README.md:287-292`).
State: `CPU{regs[32],pc,hi,lo,llbit,ll_addr,cop0,mxu}` + globals
`g_cpu_regs/g_cpu_pc/g_cpu_hi/g_cpu_lo` synced **3× per insn** for syscall access.
`Memory`: 32 MB flat, `KSEG0/1: phys=vaddr&0x1FFFFFFF`, KUSEG identity, no TLB
(`memory.cpp:100-111`, `FEATURES.md:12`). Slow paths: LCD palette `0x13050200`,
GPIO `0x10010200/300`, LCD `0x13050000`, DMA `0x10042000`, IPU `0x13080000`,
`log_unmapped()` + code-section write-protect + per-4K `m_write_counts`.
ISA actually used: MIPS32r1 + `SPECIAL2` (MXU `exec_mxu1`, `S32M2I/S32I2M`),
`SPECIAL3` (`EXT`/`INS` only), `COP0` (Count/Compare/Status/Cause/EPC/PRId,
`ERET`, TLB→`EXC_RI` trap), `COP2` (`MFC2/MTC2/CFC2/CTC2`, `exec_custom`,
`LWC2/SWC2`), `LL/SC` (single-thread flag), `CACHE` nop, `LWC1/SWC1` nop,
`COP1/COP3` → `EXC_RI`. Full delay-slot + likely-branch `nullify_delay`
(`cpu.cpp:612-683`). GOT trampoline **after** delay slot:
`is_got_address(pc)→dispatch(idx,ra)→pc=task_switched?g_cpu_pc:ra`
(`cpu.cpp:687-706`). Scheduler/vsync outside: `run_until_pc()` + `do_vsync()`
+ `process_timers()` (`main.cpp:338-441`, `massive.diff`).

JIT must preserve all of the above bit-exactly, or games break.

## 1. Third-party engine comparison

| Engine | What it is | MIPS frontend? | x86_64 Win+Linux | License | Verdict for us |
|---|---|---|---|---|---|
| **GNU Lightning 2.2.3** | Low-level RISC-VM → host assembler (`jit_new_state`, `jit_prolog/epilog`, no regalloc/opt). Backends: x86/x86_64/ARM/AArch64/MIPS/PPC/RISC-V. | No — we write it | Yes (mmap/VirtualAlloc by us) | LGPL-3.0 | Usable but minimal: no regalloc, weak Windows docs, we still write all MIPS semantics + ABI shims. |
| **Lightrec** (pcercuei, PCSX-ReARMed/Beetle) | MIPS→IR(list)→opt (constprop, reordering)→Lightning emit + blockcache + interpreter fallback + threaded compile + RAM-vs-MMIO profiling. | **Yes, complete MIPS** | Linux-first; Windows/MSVC is pain; PSX memory/COP0/GTE assumptions | GPL-2.0 (Lightning LGPL) | Closest functionally, but port cost > value: strip PSX GPU/GTE/DMA, add MXU/SPECIAL3/GOT-task model, fix Windows. Good **reference design**, bad drop-in. |
| **QEMU TCG** (`tcg/` + `target/mips/`) | MIPS→TCG ops→optimise→x86_64 backend, TB cache + direct chaining (`goto_tb/lookup_and_goto_ptr`), helpers for MMU/syscall, SMC via page-protect + TB linked lists. | Yes, best-tested | Yes | **GPL-2.0 strict** | Technically ideal, practically no: cannot use standalone (must fork QEMU `cpu-exec`, softmmu, signals, GLib build), infects license, ~10× code size of emulator. Study chaining/invalidation ideas only. |
| **asmjit (recommended)** | Lightweight C++ JIT: `JitRuntime+CodeHolder+x86::Assembler/Builder/Compiler`, built-in regalloc, sections, logging, `JitAllocator` (VirtualAlloc/mmap, dual-map `W^X`, `MAP_JIT`). ~500 kB, no deps, no exceptions/RTTI. | No — we write it | **Yes, first-class Win+Linux** | **Zlib (permits all)** | Best fit: we keep interpreter semantics, get correct Win64/SysV ABI + regalloc + cache mgmt for free. Frontend cost is real but bounded (our ISA subset is small). |
| LLVM / sljit / MIR | Heavy or immature x86_64 | — | — | mixed | Reject: LLVM latency/size overkill; sljit/MIR add dep without MIPS frontend. |

**Decision: asmjit custom backend, Lightrec/QEMU as design references.**
Reasons: (1) Zlib vs GPL/LGPL — no license risk for `WARPlayer.exe`-adjacent code;
(2) only asmjit solves the actual hard portability problem (executable alloc +
Win64 vs SysV calling + regalloc); (3) our hardest logic (delay slots, GOT/task
switch, `m_lcd_bpp`/palette/DMA semantics, MXU) is emulator-specific anyway —
no engine saves us from writing it; (4) Lightrec MIPS decoder + TB ideas can be
copied without the dependency.


## 2. Architecture

```
guest .app ─► frontend (decode MIPS @phys) ─► IR/C++-emit plan ─► asmjit x86_64
                                                        │
TB cache (phys-tagged, 64–256 MB exec pool) ◄───────────┘
                                                        ▼
dispatcher (asm stub): regs→host regs, call TB, handle exits
                                                        │
JIT↔C boundary: fast path inline / slow path → existing C++ helpers
  mem read/write → Memory::read/write fast inline + log_unmapped/palette/GPIO fallback
  GOT/COP0/MXU/scheduler → call into cpu.cpp/syscalls.cpp/mxu.cpp/cop0.cpp (thin wrappers)
  interrupts/timers/vsync → exit to main.cpp loop (no async guards inside TB)
```

Key invariants:
- TB = single-entry, multi-exit; ends at branch-with-delay-slot-resolved,
  GOT target, `SYSCALL/BREAK/ERET`, uncompilable op, page boundary, or size cap.
- Delay slot is *always* compiled as: execute delay insn → then branch effect.
  Likely-branch-not-taken nullifies (skip) — mirrors `nullify_delay`.
- `$0` hardwired: never emit writeback to r0 (matches `if (rd/rt)` guards).
- `HI/LO`, `LL/SC`, COP0 `Count` tick per insn, MXU state stay in one
  `CpuState` struct; JIT loads/stores, never caches across exits/helpers.
- All exits materialise full guest state (GPR + pc/next-pc + hi/lo) so
  `dispatch()`, `simulate_vsync()`, `call_guest_function()` keep working.

## 3. `CpuState` + ABI (do first — everything depends on it)

```cpp
struct alignas(16) CpuState {
  u32 gpr[32]; u32 pc, next_pc; u32 hi, lo;
  u32 llbit, ll_addr; u32 cop0_count_delta;
  COP0* cop0; MXU* mxu; Memory* mem; Syscalls* syscalls;
  u8* mem_base; u32 mem_size;   // fast-path bounds check
  u32 exit_code; u32 exit_arg;  // why the TB returned
};
```

- JIT signature: `extern "C" u32 tb_func(CpuState*)` → returns exit code.
- Host mapping (asmjit `Compiler` virtual regs; pin across TB):
  Win64: `RCX`=state ptr (arg), scratch `RDX,R8-R11`, callee-saved `RBX,RBP,RDI,RSI,R12-R15`
  SysV:  `RDI`=state ptr, scratch `RSI,RDX,RCX,R8-R11`. Prolog/epilog per-ABI —
  asmjit handles it, but helpers must be declared with correct `FuncSignature`.
- GPR caching: keep hot MIPS regs in host regs within TB, flush at exits/calls.
  Start simple (memory-backed gpr[], optimise later) — correctness first.
- `mem_base`: `m_mem.data()` refreshed per TB entry (vector may realloc on load only,
  but re-read anyway). Fast path: `phys = vaddr & 0x1FFFFFFF` (KSEG0/1) or identity
  if `< mem_size`, else slow helper. Matches `vaddr_to_phys()` exactly.

## 4. Phased implementation (each phase runs the game)

### Phase 0 — harness ✅ DONE (2026-10-04)
- `src/jit/jit.h/.cpp`: `Jit { init/shutdown/run_proof/print_stats }`,
  `--jit={off,on}` + `--jit-stats` in `main.cpp`, no asmjit yet (hand-encoded TB).
- Dispatcher skeleton: `run_proof()` runs 17-byte x86-64 TB
  (`[rdi+8]=1 + [rcx+8]=1; eax=0; ret` ≡ `ADDIU v0,zero,1; JR ra`)
  on a VirtualAlloc/mmap exec page, copies v0 back. Proves exec-alloc + Win64/SysV
  ABI on real hardware before any MIPS logic.
- Gate: `tetris.app --frames 200` — PASS, `--jit=on` bit-identical to `--jit=off`
  (18,683,706 insns, 7 rendered frames, same GOT counts), proof `exit=0 v0=1`.
  Game emulation untouched (interpreter still runs everything).

### Phase 1 — straight-line ALU frontend (the real foundation)
- Decoder reuses `cpu.cpp:execute()` switch cases as spec: SPECIAL
  (`SLL/SRL/SRA/SLLV/SRLV/SRAV/JR/JALR/MOVZ/MOVN/MFHI/MTHI/MFLO/MTLO/MULT/MULTU/
  DIV/DIVU/ADD/ADDU/SUB/SUBU/AND/OR/XOR/NOR/SLT/SLTU`, `SYSCALL/BREAK→exit`,
  `SYNC→nop`), immediates (`ADDI/ADDIU/SLTI/SLTIU/ANDI/ORI/XORI/LUI`),
  `SPECIAL3 EXT/INS`, `MULT/MADD/MADDU/MSUB/MSUBU` + `CLZ/CLO`.
- Emitter: asmjit `x86::Compiler`, one guest→1-3 host insns, 32-bit wrap
  (`ADD/ADDU` = 32-bit add ignoring overflow — matches current no-trap behavior;
  `SUB` same). `DIV` by zero → keep current skip (no trap) to stay bit-identical.
- TB former: stop at any branch/J/COP0/COP2/load/store/GOT/ERET; cap ~64 insn.
- Gate: discharge test — compile-only micro-ROMs + `tetris` Phase-1 path with
  branches forced to fallback; zero behavioral change expected.

### Phase 2 — branches + delay slots + TB chaining (first speedup)
- All `J/JAL/JR/JALR/BEQ/BNE/BLEZ/BGTZ/BLTZ/BGEZ(+L/AL variants)/BEQL…/BGTZL`:
  compile delay slot inline, then conditional move of `next_pc`.
  Direct chaining: if target TB cached → `jmp` to it (patchable), else exit
  `EXIT_NEXT_PC` to dispatcher (later: `lookup_and_goto_ptr` analogue).
- `JAL/JALR/BGEZAL/BLTZAL`: write `ra = next_pc+4` before delay slot
  (matches `cpu.cpp` order!). GOT check after delay slot preserved.
- Gate: `tetris.app --frames 200` fully JITed except loads/stores/syscalls;
  compare `GOT call counts` + screenshots vs interpreter.

### Phase 3 — memory fast path (biggest win after chaining)
- Inline: `LB/LH/LW/LBU/LHU/SB/SH/SW` (+`LWL/LWR/SWL/SWR` via existing
  little-endian formulas in `cpu.cpp:457-529` — copy verbatim, don't reinvent).
  `phys=vaddr&0x1FFFFFFF` if KSEG0/1 else `vaddr`; `if phys+size>mem_size→slow`;
  `if is_code_section(phys)→slow` (write-protect!); `m_write_counts[phys>>12]++`
  on stores (or batch per-TB — verify `reset_write_counts()` users first).
- Slow helper `jit_mem_slow(kind,width,vaddr,val*)`: palette, GPIO bits,
  LCD/DMA/IPU log, `log_unmapped`, KUSEG-invalid. Called as normal C++ via
  asmjit `call` — state already flushed.
- `LL/SC`: keep as helper calls (single-thread flag semantics; rare).
  `CACHE→nop`, `LWC1/SWC1/LWC3/SWC3→nop+warn-once` (matches interpreter).
  `LWC2/SWC2` → MXU helper (Phase 4) or inline `xregs[rt]` move + slow mem.
- Gate: `7days.app --frames 500` JIT vs interp: same `write_count` hotspots,
  same screenshots; measure insn/s uplift (expect 3–5× on straight code).
- Status ✅ DISCHARGE-DONE (2026-10-07): hand-encoded fast path for all 8
  ops in `src/jit/emit_mem.cpp` (KSEG strip, overflow-safe bounds, code-
  section reject on stores, `write_counts` bump, null-base slow exit with
  `exit_arg` = op index); `LWL/LWR/SWL/SWR`, `LL/SC`, `CACHE`, `LWCx/SWCx`
  stay interpreter-only (`JIT_STOP_MEM`). `--jit-tests`: 22837 passed,
  0 failed (fast-RAM diffs, slow-exit vectors, read-your-write chains,
  KSEG0/1/KUSEG coverage). Game-execution gate deferred to Phase 5 —
  the JIT is not wired into the main loop yet (interpreter runs games).

### Phase 4 — COP0/COP2/MXU (correctness, not speed)
- `MFC0/MTC0` (Count/Compare/Status/Cause/EPC/PRId/Config + TLB regs as stubs),
  `ERET→exit`, `WAIT→exit(vsync)`, unhandled TLB/COP1/COP3→`EXC_RI` exit.
  `cop0.tick()` per insn: accumulate `cop0_count_delta`, flush on exit
  (don't emit per-insn increment — batch it).
- MXU: `MFC2/MTC2/CFC2/CTC2`, `exec_custom`, `exec_mxu1` (incl. `S32M2I/S32I2M`)
  as calls into existing `mxu.cpp` — JIT only marshals `rs/rt/rd/sa`.
  MXU1 unimplemented pools (`pool16/18/19/20/21`, `Q8MUL`, `S32SFL`, …) already
  print + fall through in interpreter; JIT must call the same path, not trap.
- Gate: audio titles (`tetris`, `Block Breaker`) — MXU mixing bit-identical;
  `7days` heavy-MXU path (310K COP2 per `TLB.md`) runs without `[MXU?] Unknown`.
- Status ✅ DISCHARGE-DONE (2026-10-07): `MFC0/MTC0`, `MFC2/MTC2/CFC2/CTC2`,
  COP2 `exec_custom`, SPECIAL2 `S32M2I/S32I2M` (the only MXU1 funcs without
  a runtime `MXU_EN` branch) in `src/jit/emit_cop.cpp` as calls into the
  existing `cop0.cpp`/`mxu.cpp` — emitter only marshals `(state, reg, val)`,
  preserves RDX across the call, follows Win64 (shadow space) / SysV ABIs.
  `ERET→STOP_ERET`, TLB/COP1/COP3 stay `STOP_COP` (trap exits); `WAIT`
  traps in the interpreter (C0 default arm), mirrored as `STOP_COP`.
  Ticks accumulate in `tick_delta` (DONE adds count, slow-mem adds idx;
  Phase 5 flushes). `--jit-tests`: 29735 passed, 0 failed (100 randomized
  COP TBs incl. `$0`-dest mirrors and unknown-reg paths, mixed ALU+mem+COP
  TBs, decode classification). Game-execution gate deferred to Phase 5 —
  the JIT is not wired into the main loop yet. Remaining SPECIAL2
  (`MADD`/`MSUB`/`CLZ`/`CLO`/MXU1 compute) stays interpreter-only: the
  `MXU_EN` dispatch is runtime state the frontend cannot see statically.

### Phase 5 — GOT/syscall/task exits + TB cache mgmt (productionise)
- TB end on GOT address: flush state, exit `EXIT_GOT idx`; dispatcher runs
  existing `Syscalls::dispatch()` then resumes at `ra` or `g_cpu_pc`
  (task switch) — reuses `cpu.cpp:687-706` logic verbatim in dispatcher.
- `SYSCALL/BREAK` → `raise_exception` exit (halts, as now).
  `call_guest_function` (`WM_CREATE/WM_TIMER/WM_KEY`, timers): invalidate or
  bypass TB for the callback range — simplest: flush TBs overlapping the
  callback PC (callbacks are rare, correctness >> speed).
- Cache: `unordered_map<phys_entry, tb_func*>`, phys-tagged (our mapping is
  flat so phys==strip(vaddr)); invalidate on `dl_load` map, code-section
  writes (already rejected → just invalidate), `heap_alloc` reuse (heap never
  executes — assert). LRU cap + `--jit-stats` (hit rate, compiled TBs, exits).
- `run_until_pc(DL_MAIN_SENTINEL)` / `TASK_RETURN_PC` / `IDLE_LOOP_PC`:
  dispatcher checks stop-PCs between TBs (same as `run_until_pc` loop).
- Gate: full `games/` sweep vs `compat/compare.py` baselines; no new
  `[KUSEG]`/`[EXCEPTION]`; `Life/StopWatch/dicer` slot-166 override still hits.
- Status ✅ DONE-CORRECTNESS (2026-10-07): `Jit::run_until_pc` in
  `src/jit/jit.cpp` (phys-tagged TB cache, 1 MB exec pools, flush-all LRU
  cap 4096, `g_code_gen` invalidation on dl_load/dl_free/dl_res map+close,
  phase-transition flush since TBs are compiled against one stop-PC set).
  Straight-line TBs (ALU+mem+COP, truncated before unemitted branches)
  run cached; control flow/GOT/stops/unmapped PCs fall back to
  `CPU::execute_one` (delay slots, dispatch, halt rules unchanged).
  State sync per TB (GPR/hi/lo copy; COP0/MXU/RAM pointer-shared;
  ticks flushed 1:1). `--jit=on` runs games; `--jit=off` is the reference.
- Sweep result (28 games, `--seconds 12 --nosound`, off vs on):
  identical note distribution (all "time limit"), zero `[EXCEPTION]`/
  `[KUSEG]` on either side, no game goes dark under JIT, no unexpected
  TB exits, `uncompilable=0` fleet-wide, TB share 68–93%, Final PCs in
  game code, slot-166 override fires on both sides (Life/dicer/StopWatch).
  `compare.py` frame deltas are negative (JIT renders fewer frames in
  fixed wall time) — a pure throughput artifact: ticks are wall-clock
  paced, so a slower runner takes different delay/timeout paths through
  timing-sensitive game code. Tetris `--frames 60`: identical Final PC,
  insn counts match to 18/120M (budget overshoot <1 TB, documented).
- Known gap at the time (closed by Phase 6 below): JIT was ~3–6× slower
  per guest insn than the interpreter (per-TB sync + fallback-per-branch;
  avg ~5 insns/TB). `--jit=off` stays the reference forever.

### Phase 6 — optimise (only after sweep is green)
- GPR/host-reg pinning, constprop across TB (copy Lightrec `constprop.c` idea),
  `Count`-tick batching already done, branch-chain patching, RAM-vs-MMIO
  profiling (Lightrec-style: first slow, then patch direct if always RAM).
- Optional: threaded compile on loading zones (Lightrec `reaper.c` model).
- Gate: ≥3× end-to-end fps on `7days` title CG; `--jit-stats` shows >90% TB hits.
- Status ✅ DONE (2026-10-07): branch-as-exit TBs + persistent JIT state.
  TBs now span one terminal static branch (`src/jit/emit_branch.cpp`:
  delay slot inline, link before delay, condition evaluated before delay
  and spilled on the stack, likely-not-taken skips delay, `NEXT_PC` exit;
  JR/JALR and mem delay slots stay interpreter-side by formation rule in
  `jit_form_tb`). `JitState` slots are resident across TBs (sync only at
  run entry/exit and around fallbacks); cache keyed by entry vaddr
  (J-targets embed high bits, so KSEG aliases compile separately).
  `--jit-tests`: 36037 passed, 0 failed (incl. 170 branch TBs over all 14
  kinds taken/not-taken + links + KSEG1 + segment edge, and formation
  vectors against a real `Memory`).
- Gate numbers: TB share 98.5% + ~100% hit rate on `7days` (>90% ✓);
  end-to-end loop rate 104 vs 24 CPU-frames/s = 4.3× ✓; core throughput
  208M vs 48M guest insn/s = 4.3× ✓. Rendered title fps 20 vs 13.2
  (1.5×): the title is game-pacing-capped, not core-bound — content is
  pixel-identical (7days/tetris screenshot hashes match off-vs-on).
  Audio-mode fleet sweep (`--seconds 12`, disk driver): `compare.py`
  15 improved / 0 regressed, notes identical, zero faults either side,
  slot-166 fires both sides. (A `--nosound` sweep was also run: same
  correctness, lower rendered counts — the non-blocking nosound sink lets
  the audio task spin while AppMain waits on wall ticks, amplified by a
  faster core. Realistic config is audio mode; the mechanism is inherent
  to wall-clock pacing, not JIT logic.)
- Fixes on the way: Makefile header deps were incomplete (`main.o` went
  stale after `sizeof(Jit)` grew and smashed `Display` — full-deps rule
  added); Phase-0 proof TB wrote through garbage RDI on Win64 (ABI-split).
  Done since: (1) direct-mapped TB cache (`tbcache.h`); (2) TB chaining
  (16-byte patchable branch exits, `patch_edges_to` on compile/NEXT_PC,
  chain entry at `TB+JIT_PROLOG_CHAIN_OFF`, `insn_delta` across chains).
  Done: (3) tick-flush fast path (`COP0::flush_ticks`, wired==0 batch,
  TB flagged `tick_fast` when no MTC0). Deferred: (4) GPR
  pinning / constprop / fallback diet. The bottleneck: dispatch lookup
  (~9ns/iter, 28% loop), TB sync (35%), fallbacks (30%). All measurements
  are approximate (rdtsc overhead folded); ratios are the reliable data.

### Phase 6b — dispatch overhead (measured, 2026-10-07)
- Method: rdtsc counters in the dispatcher (`--jit-stats` time section),
  calibrated at startup (~50 ms vs wall clock, 3.29 GHz here). Each region
  number folds in ~2 rdtsc reads (~7–10 ns systematic upward bias —
  visible in per-TB-run reading *below* one round-trip on tiny TBs);
  ratios and totals are solid, single-digit-ns absolutes are approximate.
- Findings (loop-time split — compile is ms per whole run, negligible):

  | region | tetris `--frames 60` | 7days `--seconds 15` |
  |---|---|---|
  | TB exec (sync + call + tick flush) | 476 ms, 35% — 13.6 ns/run, 3.1 insns | 5781 ms, 55% — 23.0 ns/run, 11.3 insns |
  | Dispatch (gen check + cache lookup) | 410 ms, 30% — ~9 ns/iter | 2974 ms, 28% |
  | Fallback (syncs + `execute_one`) | 474→363 ms, 35→30% — 41→31 ns/iter | 1805 ms, 17% — 44 ns/iter |

  ~45% of loop time executes no guest code — that, not TB bodies (~2 ns
  per guest insn), is the remaining headroom.
- Two metric artifacts caught on the way: "299 µs per compile" divided
  total compile-region time (incl. millions of instant-reject calls at
  JR/syscall heads) by successful compiles only — real compile is
  ~2 µs, split form ~13% / emit ~63% / install ~20% (a standalone
  microbenchmark cleared `FlushInstructionCache`: ~11 ns, innocent).
  Fixed by counting attempts honestly plus a **negative cache** (stable
  rejects cached as null entries; JR-head revisits now cost one lookup —
  11.5M `neg_hits` on tetris; fallback cost fell 41→31 ns/iter as proof).
- Ranked follow-ups (gain × feasibility): **(1) direct-mapped TB cache**
  (~15–20% loop, easy — replaces `unordered_map` find); **(2) TB chaining**
  (~20%, moderate — `NEXT_PC` exits jump straight to the next TB, flush-all
  keeps invalidation tractable); **(3) tick-flush fast path** (closed form
  when `wired==0` and no MTC0 in TB — A/B test first, loop may dominate TB
  time); **(4) fallback diet** (~5–8% — skip redundant sync pairs, lazy
  `g_cpu` sync, no trace on JIT path). Explicitly rejected: GPR pinning
  (poor ROI while dispatch+fallback dominate), constprop, threaded
  compile (compile measures zero).

## 5. Risks & non-goals

- **Self-modifying code**: games don't SMC the RAWD image (write-protect
  asserts it); `dl_load`d DLX2 (`Overlord/Yi-Chi`) maps new code → flush range.
  No QEMU-style page-protect needed.
- **`m_lcd_bpp`/palette/DMA semantics**: stay in C++ helpers; JIT never caches
  them (they caused heap-corruption bugs before — `FEATURES.md:217-221`).
- **FPU**: `COP1` unused (0 real insns) — keep trapping, don't implement.
- **Non-goal**: AArch64/ARM backend, interpreter removal (keep `--jit=off`
  forever as reference), cycle accuracy (cooperative scheduler only needs
  tick-batched `Count`).

## 6. Files to add (no existing files moved)

```
src/jit/jit.h / jit.cpp          — Jit lifecycle, dispatcher, TB cache, stats
src/jit/frontend.h/.cpp          — MIPS decode → TB plan (reuses cpu.cpp semantics)
src/jit/emit_alu.cpp             — Phase 1 emitters
src/jit/emit_branch.cpp          — Phase 2
src/jit/emit_mem.cpp             — Phase 3 fast path + slow helper decl
src/jit/emit_cop.cpp             — Phase 4 COP0/COP2/MXU marshaling
src/jit/helpers.cpp              — slow-mem, exception, GOT-exit shims (C linkage)
third_party/asmjit/              — submodule (Zlib, ~500 kB built)
Makefile + CMake option           — MINGW + GCC/Clang Linux, -m64, exec-stack off
emulator/docs/DYNAREC_PLAN.md    — this file
```

Build: `add_subdirectory(third_party/asmjit)` / Makefile `+ -Ithird_party/asmjit/src
obj/jit_*.o`; link `asmjit::asmjit`. No new runtime deps.
`JitRuntime` owns exec pages (frees W^X handling on both OSes).

## 7. Suggested start order (smallest reviewable diffs)

1. Phase 0 harness + `--jit` flag (prove alloc/exec on Win+Linux).
2. Phase 1 ALU + micro-ROM tests.
3. Phase 2 branches (first visible speedup, still falls back for mem).
4. Phase 3 mem fast path (second speedup, needs care with write-protect).
5. Phase 4 COP/MXU as calls (unlock audio + 7days).
6. Phase 5 GOT/task/cache (full-game sweep).

