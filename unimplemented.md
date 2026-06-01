# Unimplemented APIs

## Status by app

| App | Imports | Unknown | Stubbed | Implemented |
|-----|---------|---------|---------|-------------|
| 7days | 72 | 0 | 27 | 45 |
| hsingtin | 72 | 0 | 27 | 45 |
| Decollation Warrior | 72 | 0 | 27 | 45 |
| Hell Striker II | 72 | 0 | 27 | 45 |
| candy | 72 | 0 | 27 | 45 |
| linkemup | 72 | 0 | 27 | 45 |
| tetris | 72 | 0 | 27 | 45 |
| ultimate_drift | 72 | 0 | 27 | 45 |
| Zhao Yun Chuan | 71 | 0 | 26 | 45 |
| brick | 77 | 0 | 28 | 49 |
| snake | 77 | 0 | 28 | 49 |
| Puzzle Bobble | 77 | 0 | 28 | 49 |
| Landlord | 77 | 0 | 28 | 49 |
| Yi-Chi King Fighter | 96 | 38 | 15 | 43 |

---

## Missing (not in GOT table)

Only Yi-Chi King Fighter imports these. Uniquely scoped — not needed for 7days or any other app.

### GUI system (µC/GUI)
- `GUI_Exec`
- `GUI_Lock`
- `GUI_Unlock`
- `GUI_TIMER_Create`
- `GUI_TIMER_Delete`
- `GUI_TIMER_Restart`
- `GUI_TIMER_SetPeriod`
- `WM_CreateWindow`
- `WM_DeleteWindow`
- `WM_DefaultProc`
- `WM_SelectWindow`
- `WM_SetFocus`
- `WM__SendMessage`
- `open_gui_key_msg`

### LCD (no-underscore naming)
- `lcd_set_frame`
- `lcd_get_frame`
- `lcd_get_bpp`
- `LCD_GetXSize`
- `LCD_GetYSize`
- `LCD_Color2Index`

### Input (no-underscore naming)
- `kbd_get_key`
- `kbd_get_status`
- `sys_judge_event`

### Kernel
- `OSFlagPost`
- `OSQCreate`
- `spin_lock_irqsave`
- `spin_unlock_irqrestore`
- `mdelay`

### System
- `cmGetSysModel`
- `cmGetSysVersion`
- `SysDisableCloseBkLight`
- `SysEnableShutDownPower`

### Module loading
- `dl_load`
- `dl_free`

### Other
- `jz_pm_pllconvert`
- `U8TOU16`
- `U8TOU32`
- `fsys_clearerr`

---

## Stubbed (in GOT, returns constant, no real work)

All 14 apps import these. Most are harmless hardware/OS stubs. A few may cause issues if actually called.

### Potentially impactful
| GOT | Name | Behavior | Risk |
|-----|------|----------|------|
| 42 | `fsys_findfirst` | returns -1 | Breaks file browsing |
| 43 | `fsys_findnext` | returns -1 | Breaks file browsing |
| 13 | `StartSwTimer` | returns 1, no timer created | Breaks timer-driven games |
| 69 | `__to_unicode_le` | returns -1 | String encoding fails |
| 70 | `__to_locale_ansi` | returns -1 | String encoding fails |
| 72 | `get_dl_handle` | returns 0 | Module loading fails |

### Harmless hardware/OS stubs
| GOT | Name | Behavior |
|-----|------|----------|
| 0 | `abort` | prints warning, returns 0 |
| 12 | `vxGoHome` | returns 0 |
| 14 | `free_irq` | returns 0 |
| 15 | `fsys_RefreshCache` | returns 0 |
| 22 | `__icache_invalidate_all` | returns 0 |
| 23 | `__dcache_writeback_all` | returns 0 |
| 24 | `TaskMediaFunStop` | returns 0 |
| 25 | `OSCPUSaveSR` | returns 0 |
| 26 | `OSCPURestoreSR` | returns 0 |
| 27 | `serial_getc` | returns 0 |
| 30 | `get_game_vol` | returns hardcoded 80 |
| 37 | `fsys_remove` | returns -1 |
| 38 | `fsys_rename` | returns -1 |
| 44 | `fsys_findclose` | returns 0 |
| 45 | `fsys_flush_cache` | returns 0 |
| 46 | `USB_Connect` | returns 0 |
| 47 | `udc_attached` | returns 0 |
| 48 | `USB_No_Connect` | returns 0 |
| 52 | `waveout_set_volume` | returns 0 |
| 53 | `HP_Mute_sw` | returns 0 |
| 57 | `pcm_ioctl` | returns 0 |
| 71 | `get_current_language` | returns hardcoded 1 |
