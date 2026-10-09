# Minor emulation-loop improvements (plan)

Lightweight, low-risk changes to host throughput and OS/audio cadence. See also `AUDIT.md` for the full timing audit.

## Done

### Split “vsync” from OS service and video

- **`Syscalls::service_os_quantum()`** — single **`SDL_GetTicks()`** per main-loop quantum:
  - Software / **GUI timers** via **`process_timers(now)`** (StartSwTimer / `GUI_Exec` path).
  - 100 Hz **`m_os_ticks`** catch-up to wall clock.
  - Task wakeups, cooperative scheduler, audio-worker slice, same-priority rotation.
  - **No** display Present (video is not VBlank).
- **`CPU::do_vsync()`** — sync `g_cpu_*` ↔ interpreter, then **`service_os_quantum()`** (name kept for call sites).
- **`main.cpp`** (Phase 1 + Phase 2) — after **`cpu.do_vsync()`**, call **`display.present_if_needed()`**.

### Lazy video (flip ≠ upload ≠ present)

- Guest **`flip*()`** → **`copy_from_guest_*()`** only (staging + **`m_dirty`** / **`m_texture_dirty`**).
- **`upload_texture_only()`** — RGB565→ARGB if needed, then **`SDL_UpdateTexture`** (main loop / screenshots only).
- **`present_if_needed()`** — upload if **`m_texture_dirty`**; **Present** only when **`m_dirty`** or **400 ms heartbeat** (heartbeat reuses last texture via **`copy_texture()`**).
- No **`SDL_RenderPresent`** inside LCD syscalls.

## Next (optional, same theme)

1. **CPU quantum** — decouple **`GUEST_INSNS_PER_SLICE`** from `GUEST_CPU_HZ / 60`; use e.g. **`GUEST_INSNS_PER_QUANTUM = 2'000'000`** for shorter batches (more OS service, less latency).
2. **Docs** — refresh `AUDIT.md` for **`present_if_needed()`** and lazy upload path.

## Do not change casually

- Scheduler bootstrap on first quantum after task registration.
- Audio-worker preemption when AppMain (prio &lt; 16) is runnable.
- Same-priority time slice (Rick Dangerous–style mixers).
- Never **`SDL_RenderPresent`** from inside guest LCD syscalls (Windows D3D).
