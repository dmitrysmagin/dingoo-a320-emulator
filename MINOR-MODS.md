# Minor emulation-loop improvements (plan)

Lightweight, low-risk changes to host throughput and OS/audio cadence. See also `AUDIT.md` for the full timing audit.

## Done

### Split “vsync” from OS service and video

- **`Syscalls::service_os_quantum()`** — single **`SDL_GetTicks()`** per main-loop quantum:
  - Software / **GUI timers** via **`process_timers(now)`** (StartSwTimer / `GUI_Exec` path).
  - 100 Hz **`m_os_ticks`** catch-up to wall clock.
  - Task wakeups, cooperative scheduler, audio-worker slice, same-priority rotation.
  - **No** `Display::present_blank()` (video is not VBlank).
- **`CPU::do_vsync()`** — sync `g_cpu_*` ↔ interpreter, then **`service_os_quantum()`** (name kept for call sites).
- **`main.cpp`** (Phase 1 + Phase 2) — after **`cpu.do_vsync()`**, call **`display.present_blank()`** so D3D still gets Copy+Present once per quantum without tying presentation to RTOS logic.
- **`simulate_vsync()`** — inline alias to **`service_os_quantum()`** for older docs/comments.

## Next (optional, same theme)

1. **CPU quantum** — decouple **`GUEST_INSNS_PER_SLICE`** from `GUEST_CPU_HZ / 60`; use e.g. **`GUEST_INSNS_PER_QUANTUM = 2'000'000`** for shorter batches (more OS service, less latency). Gate **`present_blank()`** on dirty texture so extra quanta stay cheap.
2. **Lazy LCD** — `_lcd_set_frame` / `flip()` only **stage** guest pixels; defer RGB565→ARGB + **`SDL_UpdateTexture`** until **`present_blank()`** when **`m_texture_dirty`**; coalesce multiple guest flips per quantum.
3. **Phase 1** — already runs **`do_vsync()`** + present; confirm no title needs timer-only without GPR sync (unlikely).
4. **Docs** — refresh `README.md` / `FEATURES.md` lines that describe `simulate_vsync` as including Present.

## Do not change casually

- Scheduler bootstrap on first quantum after task registration.
- Audio-worker preemption when AppMain (prio &lt; 16) is runnable.
- Same-priority time slice (Rick Dangerous–style mixers).
- Never **`SDL_RenderPresent`** from inside guest LCD syscalls (Windows D3D).
