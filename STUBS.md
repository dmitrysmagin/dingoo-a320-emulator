# Quick‑win OS‑call stubs

These APIs are currently stubbed in the emulator but can be implemented with minimal effort. Implementing them improves compatibility for many games.

| GOT | API | Current stub | Minimal implementation idea |
|-----|-----|--------------|-----------------------------|
| 42‑43 | `fsys_findfirst` / `fsys_findnext` | returns -1 | Iterate over `Archive` entries (or host `save/` dir) and return a dirent‑like struct. |
| 13 | `StartSwTimer` | prints args, returns 1 | Store timer period + callback; on each frame check SDL ticks and invoke callback. |
| 69 | `__to_unicode_le` | returns -1 | Convert guest UTF‑8 string to UTF‑16LE, allocate guest buffer via `heap_alloc`, write, return pointer. |
| 70 | `__to_locale_ansi` | returns -1 | Convert UTF‑16LE buffer to UTF‑8, allocate guest buffer, write, return pointer. |
| 72 | `get_dl_handle` | returns 0 | Allocate a dummy handle referencing the current `Archive`. |
| 73‑76 | `dl_res_open` / `dl_res_get_size` / `dl_res_get_data` / `dl_res_close` | not implemented | Thin wrappers around `Archive::find` using the handle from `get_dl_handle`. |
| 52 | `waveout_set_volume` | implemented | Volume multiplier stored; applied in `waveout_write`. |
| 53 | `HP_Mute_sw` | implemented | Sets volume multiplier to 0. |
| 57 | `pcm_ioctl` | no‑op | Return 0 (success). |
