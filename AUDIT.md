# Emulator Timing, Video, and Audio Audit

Date: 2026-10-09 (updated after minor-loop plan: quantum, lazy video, `end_cpu_quantum`)

## Scope

This is a read-only architecture audit of the current emulation loop, guest
timing, RTOS scheduler, LCD/framebuffer path, SDL presentation, and PCM audio
path.

Primary files:

- `src/main.cpp`
- `src/types.h`
- `src/cpu.cpp`
- `src/cop0.h`
- `src/syscalls.cpp`
- `src/syscalls.h`
- `src/display.cpp`
- `src/display.h`
- `src/jit/jit.cpp`

The Dingoo SDK sources under `../dingoo_sdk/` were also inspected to establish
how guest software uses `OSTimeGet`, `OSTimeDly`, LCD flips, and audio workers.

## Executive summary

The movement choppiness was primarily caused by an architectural mismatch, not
by choosing the wrong guest MHz constant alone.

**Current state (minor plan applied):** The interactive loop uses a **2M-instruction
quantum** (default `GUEST_INSNS_PER_QUANTUM_DEFAULT`, overridable via `--quantum`),
**`CPU::end_cpu_quantum()` → `Syscalls::service_os_quantum()`** (RTOS, timers,
scheduler, audio — one `SDL_GetTicks()` per quantum, **no** Present), then
**`Display::present_if_needed()`** (stage on guest flip; **upload + Present** only
when `m_dirty` or a 400 ms D3D heartbeat). bpp **4/1** LCD paths stage **ARGB**
once (`flip_argb8888` / `flip_indexed8`) without a guest RGB565 round trip.
Reported smoothness and audio stutter improved; the multi-clock model below still
applies for remaining work.

Historically the main loop treated:

```text
360,000,000 guest Hz / 60 Hz = 6,000,000 guest instructions
```

as one video interval. That assumes one guest instruction equals one CPU cycle
and that the host can execute six million emulated instructions every 16.67 ms.
Neither assumption is valid.

At the documented approximate throughputs:

- Interpreter, 35–50 M instructions/s: a full slice costs roughly 120–171 ms.
- JIT, 200 M instructions/s: a full slice costs roughly 30 ms.

Those are CPU-only costs. Timer processing and `SDL_RenderPresent` happen
afterward. A CPU-bound JIT path therefore cannot reach 60 outer iterations/s;
its theoretical ceiling is about 33/s before presentation. The interpreter is
lower still.

At the same time:

- `GetTickCount` and software timers use host wall time.
- µC/OS-II ticks advance at 100 Hz, but only when `service_os_quantum()` runs.
- Audio is consumed asynchronously by the SDL device callback.
- Host presentation blocks according to the host monitor/driver refresh.
- Guest LCD flips **stage** pixels on the syscall path; conversion/upload/Present
  run from the main loop when due (see lazy video below).

These independent clocks are not fully phase-locked. Intermediate guest frames
can still be dropped if multiple flips occur before Present, but idle quanta no
longer pay upload/Present cost.

### Implemented mitigations (minor plan)

| Item | Implementation |
|------|----------------|
| Decouple loop batch from 360M/60 | `GUEST_INSNS_PER_QUANTUM_DEFAULT` = 2M; `--quantum` |
| Short quanta + OS service | `end_cpu_quantum()` → `service_os_quantum()` each quantum |
| Single wall-clock read | `process_timers(now)` + RTOS catch-up share one `SDL_GetTicks()` |
| Lazy video | `copy_from_guest_*` on flip; `upload_texture_only` + `present_if_needed` |
| Split OS vs video | No `SDL_RenderPresent` in LCD syscalls or `service_os_quantum` |
| LCD bpp 4/1 | Direct ARGB staging; no ARGB→RGB565→ARGB double convert |

### Remaining recommended direction (larger than minor plan)

1. Central **`EmulationClock`** / deadline queue (replace scattered `SDL_GetTicks`).
2. Guest **60 Hz timeline** with explicit drop/repeat counts (not just last staged frame).
3. **Audio-master pacing** feedback on queue fill.
4. **`mdelay` / `udelay`** emulated time (still no-ops).
5. Keep **360 MHz** nominal for future cycle-accurate devices only.

## Current architecture

### Outer gameplay loop

`src/main.cpp:466–571` performs the following:

1. Poll SDL input.
2. Update guest key state.
3. Run the CPU/JIT for up to `max_insns_per_quantum` (default `GUEST_INSNS_PER_QUANTUM_DEFAULT` = 2M; CLI `--quantum`).
4. Call `CPU::end_cpu_quantum()` → `Syscalls::service_os_quantum()` (RTOS ticks, software/GUI timers, scheduler, audio — one `SDL_GetTicks()`).
5. `Display::present_if_needed()` (upload immediately before Present if staged; Present if dirty or 400 ms heartbeat).
6. Count one outer iteration; clear display dirty for screenshots if guest flipped.

Nominal 60 Hz frame insn budget (not the loop quantum) in `src/types.h`:

```cpp
static constexpr u32 GUEST_CPU_HZ          = 360000000u;
static constexpr u32 GUEST_INSNS_PER_SLICE   = GUEST_CPU_HZ / 60u;  // ~6M, reference
static constexpr u32 GUEST_INSNS_PER_QUANTUM_DEFAULT = 2000000u;
```

### CPU execution

The interpreter runs until the instruction budget, stop PC, idle PC, or halt:

- `CPU::run_until_pc`: `src/cpu.cpp:724–730`
- JIT equivalent: `src/jit/jit.cpp:500–690`

The JIT may overshoot the budget by less than one translation block.

`COP0::tick()` advances once per emulated instruction, not according to actual
JZ4730/JZ4740 cycle timing. There is no demonstrated mapping from one MIPS
instruction to one 360 MHz cycle.

### What `service_os_quantum()` actually does

`CPU::end_cpu_quantum()` calls `Syscalls::service_os_quantum()`:

- `src/cpu.cpp:731–747`
- `src/syscalls.cpp:3631–3782`

Despite its name, `service_os_quantum()` is not an LCD VBlank event. It:

1. Catches the 100 Hz RTOS tick counter up to host wall time.
2. Wakes timed and semaphore-blocked tasks.
3. Checks whether audio space became available.
4. Performs cooperative task selection.
5. Forces selected audio workers to run.
6. Rotates same-priority tasks.

Presentation is **not** here; `main.cpp` calls `Display::present_if_needed()` after `end_cpu_quantum()`.

There is no guest-visible VBlank IRQ, LCD scanout deadline, VBlank status
register transition, or framebuffer latch tied to a 60 Hz guest event.

LCD MMIO accesses in `src/memory.cpp` are primarily logged; they do not form a
complete LCD/VBlank device model.

### Guest timing APIs

The timing APIs do not share one update boundary:

- `OSTimeGet` returns `m_os_ticks`: `src/syscalls.cpp:2393–2395`.
- `m_os_ticks` catches up from `SDL_GetTicks()` at 100 Hz inside
  `service_os_quantum`: `src/syscalls.cpp:3661–3680`.
- `GetTickCount` directly returns `SDL_GetTicks() * 1000`:
  `src/syscalls.cpp:2726–2732`.
- Software timers use the same `host_now_ms` passed into `service_os_quantum()`
  (`process_timers(now)` at the start of each quantum).
- `mdelay`, `udelay`, and `delay_ms` return without delaying:
  `src/syscalls.cpp:3284–3290`, `3916`, and `3982`.
- `av_delay` converts milliseconds with `(ms + 15) / 16` and then calls
  `OSTimeDly`: `src/syscalls.cpp:3839–3847`.

The SDK declares `OS_TICKS_PER_SEC` as 100, so a tick is nominally 10 ms.
The `av_delay` conversion uses an approximately 16 ms divisor and is therefore
inconsistent with that tick base.

### Guest LCD update path

The common guest path is:

```text
_lcd_get_frame -> guest draws into RAM -> _lcd_set_frame
  -> Display::flip* (staging only, no Present)
       bpp 2: copy_from_guest_rgb565 -> m_framebuffer
       bpp 4: copy_from_guest_argb8888 -> m_argb_cache
       bpp 1: copy_from_guest_indexed8 (CLUT 0x03050100) -> m_argb_cache
main loop end_cpu_quantum -> present_if_needed
  -> upload_texture_only (if presenting and m_texture_dirty)
  -> present_to_screen (Copy + SDL_RenderPresent) if m_dirty or heartbeat
```

Relevant locations:

- `_lcd_set_frame`: `src/syscalls.cpp` (dispatches by `m_lcd_bpp`)
- Staging: `src/display.cpp` (`copy_from_guest_*`, `flip_*`)
- Upload/Present: `upload_texture_only`, `present_if_needed`, `present_to_screen`

**Rules:** Never call `SDL_RenderPresent` from inside LCD syscalls (Windows D3D).
Upload is skipped on idle quanta (no dirty frame, no heartbeat).

The renderer may use `SDL_RENDERER_PRESENTVSYNC` (`display.cpp` init). Heartbeat
Present keeps the window valid when the guest is idle.

### Audio path

Guest PCM flow:

```text
waveout_write / pcm_write
  -> audio_do_write
  -> read or convert guest PCM
  -> lock-free SPSC ring
  -> SDL audio callback
  -> host device
```

Relevant locations:

- Device/ring setup: `src/syscalls.cpp:359–523`
- Guest write path: `src/syscalls.cpp:763–797`, `2174–2240`
- SDL callback: `src/syscalls.cpp:799–846`
- Blocking/wakeup: `src/syscalls.cpp:533–700`

The ring has a physical target around 120 ms and the default guest-ahead limit
is 80 ms. The callback generally requests at most 512 frames, approximately
10.7–11.6 ms at common 44.1/48 kHz rates.

The SDL callback advances asynchronously in host wall time. A callback that
frees ring space only sets `m_audio_space_flag`; guest task wakeup is deferred
until the main thread next reaches `service_os_quantum()`.

## Findings

### Critical: the outer loop is instruction-quantized, not time-quantized

**Partially mitigated:** default quantum is **2M** insns (`--quantum`), not 6M/60.

One outer quantum’s real duration
depends on:

- Interpreter versus JIT.
- Translation-block shape and fallback rate.
- Host CPU.
- Guest workload.
- Filesystem and syscall work.
- Frame conversion/upload work.
- SDL presentation blocking.

This makes gameplay cadence depend on emulator throughput. Enabling the JIT can
change game timing rather than only reducing host CPU load.

The JIT plan already records throughput-dependent differences in fixed-wall-time
tests (`docs/DYNAREC_PLAN.md:215–253`), which is consistent with this diagnosis.

### Critical: independent clocks are not synchronized

The current implementation has at least four timing domains:

1. Instruction count and per-instruction COP0 ticks.
2. Host wall time through `SDL_GetTicks`.
3. Host audio-device callback consumption.
4. Host renderer presentation/VSync.

They meet only at coarse outer-loop boundaries. A title that combines
`GetTickCount`, `OSTimeDly`, audio semaphores, and LCD flips can therefore
observe inconsistent time progression.

### Critical: OS service was bundled with presentation (historical)

**Mitigated:** `service_os_quantum()` no longer presents. Video is
`present_if_needed()` in `main.cpp` after `end_cpu_quantum()`.

RTOS tick service, scheduler, audio wake, and time slicing still share one
quantum boundary; finer-grained event scheduling remains future work.

### Critical: 60 Hz guest output can be collapsed

**Partially mitigated:** Multiple flips per quantum still collapse to one Present,
but staging replaces prior staging and **upload/Present are skipped** on idle quanta.

Guest flips during one CPU quantum still only copy/stage (no upload until Present).

Consequences:

- Intermediate guest frames are silently dropped.
- Conversion/upload cost is paid **at most once per visible Present**, not per flip.
- The dirty flag records only “one or more flips,” not how many.
- Reported “rendered” frames are neither exact guest submissions nor exact
  unique host presentations.

### High: audio/task wake latency is tied to the CPU quantum

**Partially mitigated:** 2M default quantum (~10–60 ms host time depending on JIT)
vs former 6M slice.

The SDL callback can free audio space roughly every 11 ms, but blocked guest
writers are reconsidered only in `service_os_quantum`.

With a long CPU quantum, this can still cause:

- Late mixer wakeups.
- Bursty PCM production.
- Avoidable underruns.
- Large dependence on JIT/interpreter speed.

Audio-space flags should be checked between short CPU quanta, not once per
nominal video frame.

The callback should only publish atomic state or signal the main thread. Guest
task switching must remain on the emulation thread.

### High: scheduler wait reasons are conflated

`audio_wake_waiters()` at `src/syscalls.cpp:533–546` wakes priority-16-or-lower
workers that are:

- active,
- blocked,
- not blocked on a semaphore, and
- not marked `block_audio`.

That condition also matches an `OSTimeDly` sleeper, whose `wake_tick` is
non-zero. Audio-space availability can therefore cancel an unrelated timed
delay by clearing `blocked` and `wake_tick`.

Each task needs an explicit wait reason, for example:

- `Ready`
- `Timer`
- `Semaphore`
- `AudioSpace`
- `Idle`
- `Deleted`

An event must wake only tasks waiting for that event.

### High: host presentation cadence is not guest cadence

`SDL_RenderPresent` follows the host renderer:

- 59.94/60 Hz on many displays.
- 75/120/144 Hz on others.
- Potentially unthrottled with the software fallback.

Presenting once per outer CPU slice does not resolve these differences. If the
guest output is 60 Hz, the host presenter needs timestamp-based frame selection
and explicit repeat/drop accounting.

### High: eager frame conversion/upload in the guest path (historical)

**Mitigated:** Guest flips only **stage** (memcpy or CLUT→ARGB / guest ARGB→cache).
`upload_texture_only()` runs from `present_if_needed()` when a Present is due.

### High: 32-bit / indexed frames converted twice (historical)

**Fixed:** bpp **4** uses `flip_argb8888`; bpp **1** uses `flip_indexed8` →
`m_argb_cache` → single `SDL_UpdateTexture` at Present. No guest RGB565 pool.

### High: software timer callbacks appear to be overwritten

`process_timers()` calls `call_guest_function()`, which writes callback state
into global CPU variables (`g_cpu_pc`, registers).

`process_timers()` runs **inside** `service_os_quantum()` after
`end_cpu_quantum()` has copied interpreter state into `g_cpu_*`. Timer setup via
`call_guest_function()` still only **queues** a guest PC (does not execute inline).

Relevant sequence:

- `src/main.cpp:518–520`
- `src/syscalls.cpp:1457–1496`
- `src/cpu.cpp:731–747`

Static inspection therefore indicates that a due timer callback can be lost
before execution. Phase 1 also does not import the modified global state into
the local CPU state.

This should be confirmed with a focused timer test. The design should enqueue
callback events or mutate the canonical CPU/task context directly.

### High: Phase 1 scheduler/audio service (historical)

**Mitigated:** Phase 1 `dl_main` loop calls `end_cpu_quantum()` +
`present_if_needed()` each quantum (same as Phase 2).

If a title opens audio, creates workers, or blocks during initialization:

- `m_audio_space_flag` is not consumed.
- RTOS ticks are not advanced by the normal path.
- Blocked audio writes may not resume.

Phase 1 and Phase 2 should use the same general event-service loop, with
different stop conditions rather than different timing behavior.

### High: audio device reconfiguration can race the callback

`PCM_SET_SAMPLE_RATE` and `PCM_SET_CHANNEL` call `audio_alloc_ring()` before
`audio_open_device()`.

`audio_alloc_ring()` can reallocate `m_ring_buf` and `m_audio_scratch` while the
old SDL device callback is still active and reading the old vector storage.
`audio_open_device()` pauses/closes the old device only afterward.

This is a potential callback use-after-free/data race. The required order is:

1. Pause and close, or lock, the current device.
2. Reset/reallocate ring state.
3. Open the new device.
4. Prefill as needed.
5. Unpause.

### High: obtained audio format is not a first-class conversion boundary

The ring is allocated using the requested rate/channels before
`SDL_OpenAudioDevice`.

After opening, the code stores `obtained.freq` and `obtained.channels`, but does
not resample or rebuild already allocated latency accounting in that function.

The call currently passes zero allowed format changes, so SDL should normally
either provide the requested rate/channels or fail. The code nevertheless
accepts and logs differences, and a robust implementation should not depend on
backend behavior:

- Keep guest format separate from device format.
- Recompute device-side capacities after opening.
- Resample guest PCM when rates differ.
- Convert channel layout explicitly when channels differ.

### Medium: callback-shared non-atomic state has C++ data races

The SDL callback reads or modifies state also accessed by the main thread,
including:

- `m_audio_has_data`
- `m_audio_paused`
- `m_audio_muted`
- `m_volume`
- `m_audio_underruns`

The ring indices and sample counters are atomic, but the fields above are not.
Unsynchronized cross-thread access is undefined behavior in C++.

Use atomics for small scalar state or pause/lock the device around compound
configuration changes. Diagnostics should also be atomic or collected after
the device is closed.

### Medium: only one audio writer can be cooperatively blocked

`m_audio_block_task`, `m_audio_block_samples`, and `m_audio_block_pcm` represent
one pending write globally.

A second writer cannot queue independently and can fall into the overrun/drop
path. Model blocked writes per task or maintain a bounded pending-write queue.

### Medium: audio close can resume a blocked writer with stale return state

The normal completion path writes the successful byte count to the blocked
task’s saved `v0`. Immediate close clears `block_audio` and unblocks the task but
does not clearly assign a defined write result.

Define cancellation semantics and always set the resumed syscall return value.

### Medium: audio semaphore pacing derives timing from the last write size

`m_audio_hw_chunk_bytes` is updated from the last successful write and used to
decide when to post registered audio semaphores.

If a guest uses variable write sizes, the watermark can post too early or too
late. Semaphore events should be tied to explicit submitted buffer descriptors
or exact cumulative sample boundaries.

### Medium: audio master time needs a separate device-demand counter

`m_samples_played` advances only for real samples dequeued from the ring. It
does not advance for silence inserted during underrun.

That is appropriate for ring occupancy, but it is not a complete device clock.
For synchronization, maintain at least:

- Total device frames requested, including silence.
- Actual guest samples dequeued.
- Guest samples submitted.
- Current queue fill.

Use device-demand progress for the real-time audio clock and dequeued/submitted
counters for buffer control.

### Medium: default audio latency is high

The default guest-ahead target is 80 ms, with a physical ring around 120 ms.
This hides scheduling jitter but adds noticeable output latency.

After wake timing is fixed, target approximately 30–50 ms, with configurable
low/high watermarks and a short startup prefill.

### Medium: underrun and overrun handling is passive

Underrun inserts silence and increments a counter. Overrun can drop data when
cooperative blocking is unavailable.

There is no feedback controller that:

- Prioritizes mixer work near a low watermark.
- Throttles guest production near a high watermark.
- Applies small resampling correction for long-run drift.

### Medium: input latency is also instruction-slice-bound

`SDL_PollEvent` and guest key-memory updates happen once per outer loop. A long
CPU slice can delay input observation by tens or hundreds of milliseconds.

Short CPU quanta naturally fix this along with audio and timers.

### Medium: RTOS tick catch-up is bursty

After a long slice, `service_os_quantum()` increments `m_os_ticks` in a loop until it
reaches host time. Multiple delayed tasks can become ready at once and execute
in a burst rather than near their original deadlines.

Service due events frequently. When catching up after a host stall, compute
deadlines directly and cap expensive repeated work without changing logical
time.

### Medium: active and idle paths have very different cadence

When `OSTimeDly` leaves no runnable peer, the CPU reaches `IDLE_LOOP_PC` and
returns early from the slice. The loop then presents, often at host VSync.

When a task remains active, the loop runs up to the configured **quantum** (default
2M) before OS service/Present. The emulator therefore alternates between:

- Host-refresh-paced idle periods.
- CPU-throughput-paced active periods.

This produces visibly uneven motion.

### Medium: no-op delay APIs change guest behavior

`mdelay`, `udelay`, and `delay_ms` return immediately. Code expecting a short
busy wait can run too fast, poll aggressively, or reorder work relative to
audio/video events.

These APIs should advance or wait on emulated time according to their hardware
semantics without blocking the host event loop.

### Lower: increasing guest MHz can make cadence worse

Changing 360 MHz to 420 MHz increases the outer instruction budget from six to
seven million instructions. At fixed host throughput, that lengthens the delay
between:

- Present calls.
- Input polls.
- RTOS service.
- Audio wakeups.

It may alter individual title behavior, but it does not solve synchronization.

### Lower: the current model is not deterministic

Although the outer boundary is instruction-count-based, many guest-visible
operations read host time and audio callback state. Runs can diverge according
to host load and renderer/audio scheduling.

A deterministic mode needs a fully virtual clock and event queue. It should be
separate from interactive real-time mode.

## Answers to the proposed changes

### Should video update lazily?

**Implemented (minor plan):** Stage on flip (`copy_from_guest_*`); upload only when
`present_if_needed()` will Present; heartbeat Present without new upload when static.

Not yet implemented: sequence numbers, emulated timestamps, explicit drop/repeat metrics.

### Should the emulator stop sticking to 360 MHz for the loop batch?

**Implemented:** Interactive batch size is `GUEST_INSNS_PER_QUANTUM_DEFAULT` / `--quantum`.
360 MHz remains a **nominal** reference (`GUEST_INSNS_PER_SLICE` ≈ 6M/60 Hz frame).

Keep a nominal CPU frequency only for:

- CP0 Count/Compare timing once properly modeled.
- Cycle-based peripherals.
- Busy-loop/delay calibration.
- Optional deterministic emulation.

Interactive real-time mode should execute enough guest work to meet upcoming
events, then yield. Core speed should change host utilization and available
headroom, not gameplay speed.

## Recommended target architecture

### Central clock and event scheduler

Introduce an `EmulationClock` and deadline queue.

Interactive mode:

- Source: `SDL_GetPerformanceCounter` or an equivalent monotonic high-resolution
  clock.
- Audio active: use cumulative device demand and queue-fill error as pacing
  feedback.
- Audio inactive: use monotonic host time directly.

All guest-visible timed systems should derive from this clock:

- 100 Hz RTOS ticks.
- Software timers.
- Guest 60 Hz frame/VBlank events.
- Delays and semaphore timeouts.
- Input sampling.
- Audio worker wakeups.

Do not call `SDL_GetTicks` independently from unrelated subsystems.

### Short CPU quanta

Run until the earliest of:

- Approximately 20,000–50,000 guest instructions.
- Approximately 0.5–1 ms of host execution.
- A task block/yield.
- A guest frame submission.
- The next due emulated event.
- A stop/halt condition.

After each quantum:

1. Read the central clock.
2. Deliver all due RTOS/timer events.
3. Handle audio-space signals.
4. Poll input when due.
5. Select the next guest task.
6. Present if a host presentation deadline is due.
7. Wait efficiently if no guest work is runnable and no deadline is immediate.

The instruction limit remains a safety/preemption mechanism, not a frame.

### Split `service_os_quantum()` (future)

Presentation is already split out. Optionally replace the remainder with separate responsibilities:

- `service_rtos_ticks(now)`
- `service_software_timers(now)`
- `service_audio_events()`
- `schedule_guest_task()`
- `on_guest_vblank(now)`
- `present_if_due(now)`

This makes each subsystem testable and removes false dependencies.

### Guest video timeline

Maintain:

- Guest frame submission sequence.
- Guest submission timestamp.
- Pending host staging frame.
- Last uploaded texture sequence.
- Last presented guest sequence.
- Drop count.
- Repeat count.
- Late presentation count.

A 60 Hz guest frame period is approximately 16.666666 ms. Use an accumulated
integer/rational deadline rather than repeatedly adding rounded milliseconds.

Do not assume host refresh is exactly 60 Hz. On 75/120/144 Hz displays, select
the newest guest frame appropriate for each host presentation timestamp.
Occasional repeats are unavoidable without interpolation, but their cadence
should be even and measured.

### Audio-master feedback

When audio is active:

- Maintain a target queue fill of roughly 30–50 ms after startup.
- Below the low watermark, prioritize runnable guest mixer tasks.
- Above the high watermark, throttle guest execution or let producers block.
- Keep all guest scheduling on the emulation thread.
- Optionally apply a small bounded resampling correction using a PI controller
  if long-run device-clock drift remains.

Do not stretch gameplay time to repair an audio queue error. Correct small clock
differences in the audio conversion layer and use video drop/repeat for host
refresh mismatch.

### Compatibility modes

Useful runtime modes:

- `realtime` or `audio`: central real-time clock with audio feedback.
- `deterministic`: fully virtual event time and explicit cycle accounting.
- `unthrottled`: fastest possible execution, with audio muted or handled
  specially.

JIT on/off must not select a different timing policy.

## Suggested implementation order

### Phase 0: instrumentation

Add high-resolution tracing/counters for:

- CPU quantum start/end and instructions executed.
- Guest frame submission timestamp/sequence.
- Texture upload timestamp/sequence.
- Host present start/end and blocking duration.
- Guest-frame drops and repeats.
- RTOS tick and task wake lateness.
- Audio device frames requested.
- Audio samples submitted/dequeued.
- Queue fill in frames and milliseconds.
- Underrun/overrun counts.
- Input-poll interval.

Do this before changing behavior so the current and new systems can be compared.

### Phase 1: correctness fixes

1. Add explicit task wait reasons.
2. Prevent audio-space events from waking timer sleepers.
3. Fix software timer callback delivery.
4. Pause/close the SDL audio device before reallocating the ring.
5. Make callback-shared scalar state atomic or properly locked.
6. Define blocked-write cancellation return values.
7. Give Phase 1 and Phase 2 the same event-service mechanism.

### Phase 2: decouple the main loop

1. Introduce the high-resolution central clock.
2. Replace the six-million-instruction frame with short quanta.
3. Service RTOS, timers, audio, and input between quanta.
4. Remove host presentation from scheduler service.

### Phase 3: lazy video staging

1. Snapshot native-format guest frames into host staging buffers.
2. Track pending/latest frame generations.
3. Convert and upload only at presentation.
4. Use direct ARGB8888 upload where possible.
5. Add drop/repeat counters.

### Phase 4: audio stabilization

1. Separate guest and obtained device formats.
2. Add resampling/channel conversion where required.
3. Introduce low/high queue watermarks.
4. Lower default latency after underruns remain stable.
5. Add bounded drift correction if measurements show it is necessary.

### Phase 5: optional guest VBlank model

If title analysis shows games depending on LCD hardware timing:

1. Add a 60 Hz guest LCD event.
2. Latch the submitted front buffer at the event.
3. Update guest-visible VBlank/LCD state.
4. Raise the appropriate guest interrupt or signal.

Do not invent an IRQ until game behavior or hardware documentation establishes
which signal is required.

## Validation plan

Test at minimum:

- Interpreter and JIT.
- Sound enabled and `--nosound`.
- 44.1 kHz and 48 kHz titles.
- 60, 75, and 120/144 Hz host displays where available.
- Accelerated VSync renderer and software fallback.
- Titles using `OSTimeDly`.
- Titles busy-waiting `GetTickCount`.
- Titles with audio semaphore workers.
- Titles submitting RGB565 and ARGB8888 frames.

Acceptance criteria:

- JIT on/off changes utilization, not gameplay cadence.
- A known 60 Hz title submits frames near 16.67 ms intervals in real-time mode.
- No steady-state audio underruns after startup under normal host load.
- Audio queue fill remains bounded around its target.
- Guest-frame drops/repeats are explicitly counted.
- Input polling remains responsive during CPU-heavy scenes.
- RTOS wakeups occur near their deadlines rather than in frame-sized bursts.
- Software timer callbacks execute exactly once per due event policy.
- Audio device reconfiguration has no callback race.
- `--nosound` does not cause mixer tasks to spin or change game speed.

## Final recommendation

Do not tune choppiness by changing `GUEST_CPU_HZ`.

First replace the six-million-instruction outer frame with a short-quantum,
deadline-driven loop. Then stage guest frames and upload only the selected frame
at presentation. Service audio and RTOS events between quanta, and use explicit
task wait reasons.

That change addresses the root cause: guest progress, guest timing, audio
consumption, and host presentation currently advance on different clocks and
only meet at a coarse instruction-count boundary.
