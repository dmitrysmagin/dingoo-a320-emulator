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
- **`present_if_needed()`** — **Present** only when **`m_dirty`** or **400 ms heartbeat**; **`upload_texture_only()`** runs immediately before Present (idle quanta skip upload and Present; heartbeat without new frame reuses last texture).
- No **`SDL_RenderPresent`** inside LCD syscalls.

### CPU quantum (decoupled from 360 MHz / 60)

- Default **`GUEST_INSNS_PER_QUANTUM_DEFAULT`** = **2'000'000** insns per outer loop batch.
- **`GUEST_INSNS_PER_SLICE`** (~6M) kept as nominal one 60 Hz frame at full **`GUEST_CPU_HZ`** (reference only).
- CLI **`--quantum`** / **`--quantum=`**: plain integer (**`2000000`**) or **`M`** suffix (**`2M`**, **`2m`**, **`3M`**).

## Next (optional, same theme)

1. **Docs** — deeper **`AUDIT.md`** pass for lazy present + quantum cadence sections.

## Do not change casually

- Scheduler bootstrap on first quantum after task registration.
- Audio-worker preemption when AppMain (prio &lt; 16) is runnable.
- Same-priority time slice (Rick Dangerous–style mixers).
- Never **`SDL_RenderPresent`** from inside guest LCD syscalls (Windows D3D).
