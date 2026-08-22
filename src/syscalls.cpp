#include "syscalls.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

// fsys_find constants (mirrors dingoo_sdk/include/dingoo/fsys.h)
#define FSYS_FILENAME_MAX 544
#define FSYS_ATTR_DISKLABEL 0x08
#define FSYS_ATTR_DIR       0x10
#define FSYS_ATTR_FILE      0x20
#define FSYS_FIND_FILE      0x00
#define FSYS_FIND_DIRECTORY 0x10

extern u32 g_cpu_regs[32];
extern u32 g_cpu_pc;
extern u32 g_cpu_hi;
extern u32 g_cpu_lo;

// ── Static handler registry (class members) ──────────────────────────────

const Syscalls::GOTHandler Syscalls::s_handlers[] = {
    {"Custom_Memsic_test",      &Syscalls::impl_Custom_Memsic_test,      true},
    {"GUI_Exec",                &Syscalls::impl_GUI_Exec,                false},
    {"GUI_Lock",                &Syscalls::impl_GUI_Lock,                false},
    {"GUI_TIMER_Create",        &Syscalls::impl_GUI_TIMER_Create,        false},
    {"GUI_TIMER_Delete",        &Syscalls::impl_GUI_TIMER_Delete,        false},
    {"GUI_TIMER_Exec",          &Syscalls::impl_GUI_TIMER_Exec,          true},
    {"GUI_TIMER_Restart",       &Syscalls::impl_GUI_TIMER_Restart,       false},
    {"GUI_TIMER_SetPeriod",     &Syscalls::impl_GUI_TIMER_SetPeriod,     false},
    {"GUI_Unlock",              &Syscalls::impl_GUI_Unlock,              false},
    {"GetTickCount",            &Syscalls::impl_GetTickCount,            false},
    {"Get_X",                   &Syscalls::impl_Get_X,                   true},
    {"Get_Y",                   &Syscalls::impl_Get_Y,                   true},
    {"HP_Mute_sw",              &Syscalls::impl_HP_Mute_sw,              false},
    {"LCD_Color2Index",         &Syscalls::impl_LCD_Color2Index,         false},
    {"LCD_GetXSize",            &Syscalls::impl_LCD_GetXSize,            false},
    {"LCD_GetYSize",            &Syscalls::impl_LCD_GetYSize,            false},
    {"LcdGetDisMode",           &Syscalls::impl_LcdGetDisMode,           false},
    {"Memsic_SerialCommInit",   &Syscalls::impl_Memsic_SerialCommInit,   true},
    {"OSCPURestoreSR",          &Syscalls::impl_OSCPURestoreSR,          false},
    {"OSCPUSaveSR",             &Syscalls::impl_OSCPUSaveSR,             false},
    {"OSFlagPost",              &Syscalls::impl_OSFlagPost,              true},
    {"OSQCreate",               &Syscalls::impl_OSQCreate,               true},
    {"OSSemCreate",             &Syscalls::impl_OSSemCreate,             false},
    {"OSSemDel",                &Syscalls::impl_OSSemDel,                false},
    {"OSSemPend",               &Syscalls::impl_OSSemPend,               false},
    {"OSSemPost",               &Syscalls::impl_OSSemPost,               false},
    {"OSTaskCreate",            &Syscalls::impl_OSTaskCreate,            false},
    {"OSTaskDel",               &Syscalls::impl_OSTaskDel,               false},
    {"OSTimeDly",               &Syscalls::impl_OSTimeDly,               false},
    {"OSTimeGet",               &Syscalls::impl_OSTimeGet,               false},
    {"Read_Acc",                &Syscalls::impl_Read_Acc,                true},
    {"Read_Acc0",               &Syscalls::impl_Read_Acc0,               true},
    {"StartSwTimer",            &Syscalls::impl_StartSwTimer,            false},
    {"SysDisableBkLight",       &Syscalls::impl_SysDisableBkLight,       true},
    {"SysDisableCloseBkLight",  &Syscalls::impl_SysDisableCloseBkLight,  true},
    {"SysEnableShutDownPower",  &Syscalls::impl_SysEnableShutDownPower,  true},
    {"TaskMediaFunStop",        &Syscalls::impl_TaskMediaFunStop,        true},
    {"U8TOU16",                 &Syscalls::impl_U8TOU16,                 false},
    {"U8TOU32",                 &Syscalls::impl_U8TOU32,                 false},
    {"USB_Connect",             &Syscalls::impl_USB_Connect,             true},
    {"USB_No_Connect",          &Syscalls::impl_USB_No_Connect,          true},
    {"WM_CreateWindow",         &Syscalls::impl_WM_CreateWindow,         false},
    {"WM_DefaultProc",          &Syscalls::impl_WM_DefaultProc,          true},
    {"WM_DeleteWindow",         &Syscalls::impl_WM_DeleteWindow,         true},
    {"WM_SelectWindow",         &Syscalls::impl_WM_SelectWindow,         true},
    {"WM_SetFocus",             &Syscalls::impl_WM_SetFocus,             true},
    {"WM__SendMessage",         &Syscalls::impl_WM__SendMessage,         false},
    {"__dcache_writeback_all",  &Syscalls::impl___dcache_writeback_all,  true},
    {"__icache_invalidate_all", &Syscalls::impl___icache_invalidate_all, true},
    {"__to_locale_ansi",        &Syscalls::impl___to_locale_ansi,        false},
    {"__to_unicode_le",         &Syscalls::impl___to_unicode_le,         false},
    {"_kbd_get_key",            &Syscalls::impl__kbd_get_key,            false},
    {"_kbd_get_status",         &Syscalls::impl__kbd_get_status,         false},
    {"_lcd_get_frame",          &Syscalls::impl__lcd_get_frame,          false},
    {"_lcd_set_frame",          &Syscalls::impl__lcd_set_frame,          false},
    {"_sys_judge_event",        &Syscalls::impl__sys_judge_event,        false},
    {"_tcscmp",                 &Syscalls::impl__tcscmp,                 true},
    {"_tcscpy",                 &Syscalls::impl__tcscpy,                 true},
    {"_waveout_open",           &Syscalls::impl__waveout_open,           false},
    {"_waveout_set_volume",     &Syscalls::impl__waveout_set_volume,     false},
    {"abort",                   &Syscalls::impl_abort,                   false},
    {"ap_lcd_set_frame",        &Syscalls::impl_ap_lcd_set_frame,        false},
    {"av_begin_thread",         &Syscalls::impl_av_begin_thread,         true},
    {"av_create_flag",          &Syscalls::impl_av_create_flag,          true},
    {"av_create_sem",           &Syscalls::impl_av_create_sem,           true},
    {"av_delay",                &Syscalls::impl_av_delay,                true},
    {"av_destroy_flag",         &Syscalls::impl_av_destroy_flag,         true},
    {"av_destroy_sem",          &Syscalls::impl_av_destroy_sem,          true},
    {"av_end_thread",           &Syscalls::impl_av_end_thread,           true},
    {"av_give_flag",            &Syscalls::impl_av_give_flag,            true},
    {"av_give_sem",             &Syscalls::impl_av_give_sem,             true},
    {"av_queue_abort",          &Syscalls::impl_av_queue_abort,          true},
    {"av_queue_end",            &Syscalls::impl_av_queue_end,            true},
    {"av_queue_flush",          &Syscalls::impl_av_queue_flush,          true},
    {"av_queue_get",            &Syscalls::impl_av_queue_get,            true},
    {"av_queue_init",           &Syscalls::impl_av_queue_init,           true},
    {"av_queue_put",            &Syscalls::impl_av_queue_put,            true},
    {"av_reg_object",           &Syscalls::impl_av_reg_object,           true},
    {"av_resize_packet",        &Syscalls::impl_av_resize_packet,        true},
    {"av_uft8_2_unicode",       &Syscalls::impl_av_uft8_2_unicode,       true},
    {"av_unreg_object",         &Syscalls::impl_av_unreg_object,         true},
    {"av_upper_4cc",            &Syscalls::impl_av_upper_4cc,            true},
    {"av_wait_flag",            &Syscalls::impl_av_wait_flag,            true},
    {"av_wait_sem",             &Syscalls::impl_av_wait_sem,             true},
    {"av_wait_sem2",            &Syscalls::impl_av_wait_sem2,            true},
    {"cmGetSysModel",           &Syscalls::impl_cmGetSysModel,           false},
    {"cmGetSysVersion",         &Syscalls::impl_cmGetSysVersion,         false},
    {"delay_ms",                &Syscalls::impl_delay_ms,                true},
    {"detect_clock",            &Syscalls::impl_detect_clock,            true},
    {"dl_free",                 &Syscalls::impl_dl_free,                 false},
    {"dl_get_proc",             &Syscalls::impl_dl_get_proc,             false},
    {"dl_load",                 &Syscalls::impl_dl_load,                 false},
    {"dl_res_close",            &Syscalls::impl_dl_res_close,            false},
    {"dl_res_get_data",         &Syscalls::impl_dl_res_get_data,         false},
    {"dl_res_get_size",         &Syscalls::impl_dl_res_get_size,         false},
    {"dl_res_open",             &Syscalls::impl_dl_res_open,             false},
    {"fprintf",                 &Syscalls::impl_fprintf,                 false},
    {"fread",                   &Syscalls::impl_fread,                   false},
    {"free",                    &Syscalls::impl_free,                    false},
    {"free_irq",                &Syscalls::impl_free_irq,                true},
    {"fseek",                   &Syscalls::impl_fseek,                   false},
    {"fsys_RefreshCache",       &Syscalls::impl_fsys_RefreshCache,       true},
    {"fsys_clearerr",           &Syscalls::impl_fsys_clearerr,           true},
    {"fsys_fclose",             &Syscalls::impl_fsys_fclose,             false},
    {"fsys_fcloseW",            &Syscalls::impl_fsys_fcloseW,            true},
    {"fsys_fclose_flash",       &Syscalls::impl_fsys_fclose_flash,       true},
    {"fsys_feof",               &Syscalls::impl_fsys_feof,               false},
    {"fsys_ferror",             &Syscalls::impl_fsys_ferror,             false},
    {"fsys_findclose",          &Syscalls::impl_fsys_findclose,          true},
    {"fsys_findfirst",          &Syscalls::impl_fsys_findfirst,          true},
    {"fsys_findnext",           &Syscalls::impl_fsys_findnext,           true},
    {"fsys_flush_cache",        &Syscalls::impl_fsys_flush_cache,        true},
    {"fsys_fopen",              &Syscalls::impl_fsys_fopen,              false},
    {"fsys_fopenW",             &Syscalls::impl_fsys_fopenW,             false},
    {"fsys_fopen_flash",        &Syscalls::impl_fsys_fopen_flash,        true},
    {"fsys_fread",              &Syscalls::impl_fsys_fread,              false},
    {"fsys_fseek",              &Syscalls::impl_fsys_fseek,              false},
    {"fsys_ftell",              &Syscalls::impl_fsys_ftell,              false},
    {"fsys_fwrite",             &Syscalls::impl_fsys_fwrite,             false},
    {"fsys_mkdir",              &Syscalls::impl_fsys_mkdir,              true},
    {"fsys_remove",             &Syscalls::impl_fsys_remove,             false},
    {"fsys_removeW",            &Syscalls::impl_fsys_removeW,            true},
    {"fsys_rename",             &Syscalls::impl_fsys_rename,             false},
    {"fsys_renameW",            &Syscalls::impl_fsys_renameW,            true},
    {"fwrite",                  &Syscalls::impl_fwrite,                  false},
    {"get_current_language",    &Syscalls::impl_get_current_language,    false},
    {"get_dl_handle",           &Syscalls::impl_get_dl_handle,           false},
    {"get_game_vol",            &Syscalls::impl_get_game_vol,            false},
    {"isTVON",                  &Syscalls::impl_isTVON,                  true},
    {"jz_pm_pllconvert",        &Syscalls::impl_jz_pm_pllconvert,        true},
    {"kbd_get_key",             &Syscalls::impl_kbd_get_key,             false},
    {"kbd_get_status",          &Syscalls::impl_kbd_get_status,          false},
    {"lcd_flip",                &Syscalls::impl_lcd_flip,                false},
    {"lcd_get_bpp",             &Syscalls::impl_lcd_get_bpp,             false},
    {"lcd_get_cframe",          &Syscalls::impl_lcd_get_cframe,          false},
    {"lcd_get_frame",           &Syscalls::impl_lcd_get_frame,           false},
    {"lcd_set_frame",           &Syscalls::impl_lcd_set_frame,           false},
    {"malloc",                  &Syscalls::impl_malloc,                  false},
    {"mdelay",                  &Syscalls::impl_mdelay,                  false},
    {"memcpy",                  &Syscalls::impl_memcpy,                  true},
    {"memset",                  &Syscalls::impl_memset,                  true},
    {"open_gui_key_msg",        &Syscalls::impl_open_gui_key_msg,        false},
    {"pcm_can_read",            &Syscalls::impl_pcm_can_read,            false},
    {"pcm_can_write",           &Syscalls::impl_pcm_can_write,           false},
    {"pcm_ioctl",               &Syscalls::impl_pcm_ioctl,               false},
    {"pcm_read",                &Syscalls::impl_pcm_read,                false},
    {"pcm_write",               &Syscalls::impl_pcm_write,               false},
    {"printf",                  &Syscalls::impl_printf,                  false},
    {"realloc",                 &Syscalls::impl_realloc,                 false},
    {"serial_getc",             &Syscalls::impl_serial_getc,             true},
    {"serial_putc",             &Syscalls::impl_serial_putc,             false},
    {"serial_puts",             &Syscalls::impl_serial_puts,             true},
    {"spin_lock_irqsave",       &Syscalls::impl_spin_lock_irqsave,       true},
    {"spin_unlock_irqrestore",  &Syscalls::impl_spin_unlock_irqrestore,  true},
    {"sprintf",                 &Syscalls::impl_sprintf,                 false},
    {"sscanf",                  &Syscalls::impl_sscanf,                  true},
    {"strlen",                  &Syscalls::impl_strlen,                  false},
    {"strncasecmp",             &Syscalls::impl_strncasecmp,             false},
    {"sys_get_ccpmp_config",    &Syscalls::impl_sys_get_ccpmp_config,    true},
    {"sys_judge_event",         &Syscalls::impl_sys_judge_event,         false},
    {"tv_close",                &Syscalls::impl_tv_close,                true},
    {"tv_disable_switch",       &Syscalls::impl_tv_disable_switch,       true},
    {"tv_enable_switch",        &Syscalls::impl_tv_enable_switch,        true},
    {"tv_get_closeflag",        &Syscalls::impl_tv_get_closeflag,        true},
    {"tv_get_openflag",         &Syscalls::impl_tv_get_openflag,         true},
    {"tv_open",                 &Syscalls::impl_tv_open,                 true},
    {"tv_set_closeflag",        &Syscalls::impl_tv_set_closeflag,        true},
    {"tv_set_openflag",         &Syscalls::impl_tv_set_openflag,         true},
    {"udc_attached",            &Syscalls::impl_udc_attached,            true},
    {"udelay",                  &Syscalls::impl_udelay,                  true},
    {"vsprintf",                &Syscalls::impl_vsprintf,                true},
    {"vxGoHome",                &Syscalls::impl_vxGoHome,                true},
    {"waveout_can_write",       &Syscalls::impl_waveout_can_write,       false},
    {"waveout_close",           &Syscalls::impl_waveout_close,           false},
    {"waveout_close_at_once",   &Syscalls::impl_waveout_close_at_once,   false},
    {"waveout_get_volume",      &Syscalls::impl_waveout_get_volume,      false},
    {"waveout_open",            &Syscalls::impl_waveout_open,            false},
    {"waveout_reset",           &Syscalls::impl_waveout_reset,           false},
    {"waveout_set_volume",      &Syscalls::impl_waveout_set_volume,      false},
    {"waveout_write",           &Syscalls::impl_waveout_write,           false},
};
const int Syscalls::s_handler_count =
    sizeof(Syscalls::s_handlers) / sizeof(Syscalls::s_handlers[0]);

int Syscalls::find_handler(const char* name) {
    for (int i = 0; i < s_handler_count; i++) {
        if (strcmp(name, s_handlers[i].name) == 0) return i;
    }
    return -1;
}

void Syscalls::init_slot_handlers(const std::vector<ImportEntry>& imports) {
    m_slot_handlers.reserve(imports.size());
    for (size_t i = 0; i < imports.size(); i++) {
        int hi = find_handler(imports[i].name.c_str());
        m_slot_handlers.push_back(hi);
    }
}

Syscalls::Syscalls(Memory& mem, Display& display)
    : m_mem(mem)
    , m_display(display)
    , m_heap_top(0x00020000)  // phys: zone1 above exception vectors, zone2 at 0x04000000 (above archive)
    , m_lcd_bpp(2)       // default RGB565 — updated by rgb_user_init
    , m_lcd_back(false)
    , m_lcd_pending_buf(0)
    , m_nosound(false)
    , m_audio_open(false)
    , m_audio_device_open(false)
    , m_audio_write_count(0)
    , m_audio_sample_rate(44100)
    , m_audio_channels(2)
    , m_audio_bits(16)
    , m_volume_level(100)
    , m_audio_paused(false)
    , m_audio_muted(false)
    , m_audio_device(0)
    , m_audio_target_latency_ms(AUDIO_TARGET_LATENCY_MS_DEFAULT)
    , m_ring_cap(0)
    , m_ring_mask(0)
    , m_ring_head(0)
    , m_ring_tail(0)
    , m_samples_played(0)
    , m_samples_written(0)
    , m_audio_space_flag(false)
    , m_volume(1.0f)
    , m_audio_underruns(0)
    , m_audio_overruns(0)
    , m_audio_high_water(0)
    , m_audio_block_count(0)
    , m_audio_unblock_count(0)
    , m_audio_sem_post_count(0)
    , m_last_waveout_bytes(-1)
    , m_audio_hw_chunk_bytes(800)
    , m_audio_post_watermark(0)
    , m_audio_sem_reg_count(0)
    , m_audio_has_data(false)
    , m_audio_start_tick(0)
    , m_audio_block_task(-1)
    , m_audio_block_samples(0)
    , m_got_call_count(0)
    , m_current_task(-1)
    , m_task_count(0)
    , m_os_ticks(0)
    , m_start_tick(SDL_GetTicks())
    , m_scheduler_started(false)
    , m_task_switched(false)
    , m_in_idle(false)
    , m_idle_pc(0)
    , m_last_timer_tick(SDL_GetTicks())
    , m_wm_callback(0)
    , m_wm_paint_pending(false)
    , m_wm_msg_buf(0)
    , m_gui_timer_msg_buf(0)
    , m_wm_key_info_buf(0)
    , m_gui_key_msg_open(false)
{
    memset(m_got_call_counts, 0, sizeof(m_got_call_counts));
    m_audio_sem_reg_count = 0;
    for (int i = 0; i < MAX_TASKS; i++) {
        m_tasks[i].active = false;
        m_tasks[i].wake_tick = 0;
        m_tasks[i].block_sem = 0;
        m_tasks[i].sem_err_ptr = 0;
        m_tasks[i].block_audio = false;
    }
    for (int i = 0; i < 64; i++) {
        m_files[i].in_use = false;
        m_files[i].is_host = false;
        m_files[i].is_archive = false;
        m_files[i].host_file = nullptr;
        m_files[i].archive = nullptr;
        m_files[i].archive_entry = nullptr;
        m_files[i].offset = 0;
    }
    for (int i = 0; i < FB_POOL_SIZE; i++) {
        m_fb_pool[i].phys = 0;
        m_fb_pool[i].size = 0;
        m_fb_pool[i].in_use = false;
    }
    m_lcd_hw_buf[0] = m_lcd_hw_buf[1] = 0;  // allocated lazily on first _lcd_get_frame call
    for (int i = 0; i < MAX_DL_RES; i++) {
        m_dl_res[i].in_use = false;
        m_dl_res[i].guest_addr = 0;
        m_dl_res[i].size = 0;
        m_dl_res[i].offset = 0;
        m_dl_res[i].host_data = nullptr;
    }
    for (int i = 0; i < MAX_DL_MODULES; i++) {
        m_dl_modules[i].in_use = false;
        m_dl_modules[i].guest_addr = 0;
        m_dl_modules[i].size = 0;
    }
    m_archive = nullptr;
    m_audio_open = false;
}

void Syscalls::shutdown_audio() {
    if (m_audio_underruns || m_audio_overruns || m_audio_high_water
        || m_audio_block_count || m_audio_unblock_count || m_audio_sem_post_count) {
        printf("[AUDIO] stats: underruns=%u overruns=%u high_water=%u/%u blocks=%u unblocks=%u sem_posts=%u\n",
               m_audio_underruns, m_audio_overruns, m_audio_high_water, m_ring_cap,
               m_audio_block_count, m_audio_unblock_count, m_audio_sem_post_count);
    }
    audio_close_immediate();
    m_ring_buf.reset();
    m_audio_scratch.reset();
    m_ring_cap = m_ring_mask = 0;
    if (SDL_WasInit(SDL_INIT_AUDIO))
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

// --- Audio helpers ---

void Syscalls::set_audio_target_latency_ms(int ms) {
    if (ms < AUDIO_TARGET_LATENCY_MS_MIN)
        ms = AUDIO_TARGET_LATENCY_MS_MIN;
    else if (ms > AUDIO_TARGET_LATENCY_MS_MAX)
        ms = AUDIO_TARGET_LATENCY_MS_MAX;
    m_audio_target_latency_ms = ms;
}

int Syscalls::audio_calc_ring_cap(u32 rate, u32 channels) const {
    uint64_t need = (uint64_t)rate * channels * AUDIO_RING_MS / 1000;
    if (need < (uint64_t)AUDIO_MIN_RING_CAP)
        need = AUDIO_MIN_RING_CAP;
    u32 cap = 1;
    while (cap < need && cap < (u32)AUDIO_MAX_RING_CAP)
        cap <<= 1;
    if (cap > (u32)AUDIO_MAX_RING_CAP)
        cap = AUDIO_MAX_RING_CAP;
    return (int)cap;
}

void Syscalls::audio_alloc_ring(u32 rate, u32 channels) {
    int cap = audio_calc_ring_cap(rate, channels);
    m_ring_cap = (u32)cap;
    m_ring_mask = m_ring_cap - 1;
    m_ring_buf = std::make_unique<s16[]>(m_ring_cap);
    m_audio_scratch = std::make_unique<s16[]>(AUDIO_MAX_CHUNK_SAMPLES);
    m_audio_high_water = 0;
    m_audio_has_data = false;
    audio_reset_ring();
    printf("[AUDIO] ring cap=%u samples (target=%ums ring=%ums @ %uHz %uch)\n",
           m_ring_cap, m_audio_target_latency_ms, AUDIO_RING_MS, rate, channels);
}

void Syscalls::audio_reset_ring() {
    m_ring_head.store(0, std::memory_order_relaxed);
    m_ring_tail.store(0, std::memory_order_relaxed);
    uint64_t played = m_samples_played.load(std::memory_order_relaxed);
    m_samples_written.store(played, std::memory_order_relaxed);
    m_audio_has_data = false;
}

int Syscalls::audio_ring_used() const {
    if (!m_ring_cap) return 0;
    uint32_t head = m_ring_head.load(std::memory_order_acquire);
    uint32_t tail = m_ring_tail.load(std::memory_order_acquire);
    return (int)(tail - head);
}

int Syscalls::audio_ring_free() const {
    if (!m_ring_cap) return 0;
    return (int)m_ring_cap - audio_ring_used() - 1;
}

u32 Syscalls::audio_max_ahead_samples() const {
    u64 max_ahead = (u64)m_audio_sample_rate * m_audio_channels
                    * (u64)m_audio_target_latency_ms / 1000;
    if (max_ahead < 1)
        max_ahead = 1;
    return (u32)max_ahead;
}

int Syscalls::audio_can_write_bytes() const {
    if (!m_ring_cap || !m_audio_open)
        return 0;
    int ring_free = audio_ring_free();
    if (ring_free <= 0)
        return 0;

    uint64_t played = m_samples_played.load(std::memory_order_acquire);
    uint64_t written = m_samples_written.load(std::memory_order_acquire);
    uint64_t ahead = written - played;
    u32 max_ahead = audio_max_ahead_samples();
    if (ahead >= max_ahead)
        return 0;

    u64 latency_free = max_ahead - ahead;
    u64 free_samples = (u64)ring_free;
    if (latency_free < free_samples)
        free_samples = latency_free;
    if (free_samples > 0x7FFF)
        return 65536;
    return (int)(free_samples * sizeof(s16));
}

float Syscalls::audio_gain() const {
    if (m_audio_muted || m_nosound)
        return 0.0f;
    return m_volume;
}

int Syscalls::audio_push_pcm_once(const s16* src, int sample_count) {
    if (!src || sample_count <= 0 || !m_ring_buf || !m_ring_cap)
        return 0;

    uint32_t head = m_ring_head.load(std::memory_order_acquire);
    uint32_t tail = m_ring_tail.load(std::memory_order_relaxed);
    uint32_t used = tail - head;
    uint32_t free = m_ring_cap - used - 1;
    if ((uint32_t)sample_count > free)
        return 0;

    uint64_t played = m_samples_played.load(std::memory_order_acquire);
    uint64_t written = m_samples_written.load(std::memory_order_relaxed);
    uint64_t ahead = written - played;
    u32 max_ahead = audio_max_ahead_samples();
    if (ahead + (uint64_t)sample_count > max_ahead)
        return 0;

    uint32_t idx = tail & m_ring_mask;
    uint32_t first = std::min((uint32_t)sample_count, m_ring_cap - idx);
    memcpy(&m_ring_buf[idx], src, first * sizeof(s16));
    if ((uint32_t)sample_count > first)
        memcpy(&m_ring_buf[0], src + first, (uint32_t)(sample_count - first) * sizeof(s16));

    m_ring_tail.store(tail + (uint32_t)sample_count, std::memory_order_release);
    m_samples_written.store(written + (uint64_t)sample_count, std::memory_order_relaxed);

    uint32_t new_used = used + (uint32_t)sample_count;
    if (new_used > m_audio_high_water)
        m_audio_high_water = new_used;

    m_audio_space_flag.store(true, std::memory_order_relaxed);
    m_audio_has_data = true;
    return sample_count * (int)sizeof(s16);
}

bool Syscalls::audio_open_device(int sample_rate, int channels) {
    if (m_audio_device > 0) {
        SDL_PauseAudioDevice(m_audio_device, 1);
        SDL_CloseAudioDevice(m_audio_device);
        m_audio_device = 0;
    }

    SDL_AudioSpec want, obtained;
    SDL_zero(want);
    want.freq = sample_rate;
    want.format = AUDIO_S16SYS;
    want.channels = (Uint8)channels;
    // ~20–32 ms per SDL callback (power-of-two, clamped).
    int sdl_frames = sample_rate / 50;
    if (sdl_frames < 128)
        sdl_frames = 128;
    else if (sdl_frames > 512)
        sdl_frames = 512;
    int pow2 = 128;
    while (pow2 * 2 <= sdl_frames)
        pow2 *= 2;
    want.samples = (Uint16)pow2;
    want.callback = audio_callback;
    want.userdata = this;

    m_audio_device = SDL_OpenAudioDevice(nullptr, 0, &want, &obtained, 0);
    if (m_audio_device == 0)
        return false;

    m_audio_sample_rate = (u32)obtained.freq;
    m_audio_channels = (u32)obtained.channels;
    if (obtained.freq != sample_rate || obtained.channels != (Uint8)channels) {
        printf("[AUDIO] device adjusted: wanted %dHz %dch %d samples, got %dHz %dch %d samples\n",
               sample_rate, channels, pow2, obtained.freq, obtained.channels, obtained.samples);
    }
    SDL_PauseAudioDevice(m_audio_device, m_audio_paused ? 1 : 0);
    return true;
}

void Syscalls::audio_write_os_state(bool playing) {
    m_mem.write_u32(0x80242AE4, playing ? 1u : 0u);
    m_mem.write_u32(0x80242558, playing ? 1u : 0u);
    m_mem.write_u32(0x80242560, playing ? 0u : 1u);
    m_mem.write_u32(0x80242580, playing ? 1u : 0u);
}

void Syscalls::audio_wake_waiters() {
    audio_try_complete_blocked_writes();
    audio_post_if_chunks_played();
    if (audio_can_write_bytes() == 0)
        return;
    for (int i = 0; i < m_task_count; i++) {
        Task& t = m_tasks[i];
        if (!t.active || !t.blocked || t.block_sem || t.block_audio)
            continue;
        if (t.task_prio >= 16) {
            t.blocked = false;
            t.wake_tick = 0;
        }
    }
}

void Syscalls::audio_register_sem(u32 sem_ptr) {
    if (!sem_ptr)
        return;
    for (int i = 0; i < m_audio_sem_reg_count; i++) {
        if (m_audio_sem_reg[i] == sem_ptr)
            return;
    }
    if (m_audio_sem_reg_count >= AUDIO_SEM_REG_MAX)
        return;
    m_audio_sem_reg[m_audio_sem_reg_count++] = sem_ptr;
}

bool Syscalls::sem_signal(u32 sem_ptr) {
    if (!sem_ptr)
        return true;

    int waiter = -1;
    u8 best_prio = 0xFF;
    for (int i = 0; i < m_task_count; i++) {
        if (m_tasks[i].blocked && m_tasks[i].block_sem == sem_ptr
            && m_tasks[i].task_prio < best_prio) {
            best_prio = m_tasks[i].task_prio;
            waiter = i;
        }
    }

    if (waiter >= 0) {
        m_tasks[waiter].blocked = false;
        m_tasks[waiter].block_sem = 0;
        m_tasks[waiter].wake_tick = 0;
        m_tasks[waiter].regs[2] = 0;
        if (m_tasks[waiter].sem_err_ptr)
            m_mem.write_u8(m_tasks[waiter].sem_err_ptr, 0);
        m_tasks[waiter].sem_err_ptr = 0;
        return true;
    }

    u16 cnt = m_mem.read_u16(sem_ptr + 8);
    if (cnt >= 65535u)
        return false;
    m_mem.write_u16(sem_ptr + 8, (u16)(cnt + 1));
    return true;
}

void Syscalls::audio_post_buffer_sems() {
    if (!m_audio_open || m_audio_sem_reg_count <= 0)
        return;

    int waiter = -1;
    u8 best_prio = 0xFF;
    u32 wake_sem = 0;
    for (int i = 0; i < m_task_count; i++) {
        Task& t = m_tasks[i];
        if (!t.blocked || !t.block_sem || t.task_prio < 16)
            continue;
        for (int j = 0; j < m_audio_sem_reg_count; j++) {
            if (t.block_sem == m_audio_sem_reg[j] && t.task_prio < best_prio) {
                best_prio = t.task_prio;
                waiter = i;
                wake_sem = t.block_sem;
                break;
            }
        }
    }

    if (waiter >= 0) {
        sem_signal(wake_sem);
        m_audio_sem_post_count++;
        return;
    }

    u32 sem_ptr = m_audio_sem_reg[m_audio_sem_post_count % m_audio_sem_reg_count];
    sem_signal(sem_ptr);
    m_audio_sem_post_count++;
}

void Syscalls::audio_post_if_chunks_played() {
    if (!m_audio_open || m_audio_sem_reg_count <= 0)
        return;

    int chunk_bytes = (int)m_audio_hw_chunk_bytes;
    if (chunk_bytes <= 0)
        chunk_bytes = 800;
    int chunk_samples = (m_audio_bits == 8) ? chunk_bytes : (chunk_bytes / 2);
    if (chunk_samples <= 0)
        return;

    uint64_t played = m_samples_played.load(std::memory_order_acquire);
    uint64_t delta = played - m_audio_post_watermark;
    if (delta < (uint64_t)chunk_samples)
        return;

    int chunks = (int)(delta / (uint64_t)chunk_samples);
    m_audio_post_watermark += (uint64_t)chunks * (uint64_t)chunk_samples;
    for (int i = 0; i < chunks; i++)
        audio_post_buffer_sems();
}

bool Syscalls::audio_try_complete_blocked_writes() {
    if (m_audio_block_task < 0 || m_audio_block_task >= m_task_count)
        return false;
    if (!m_audio_block_pcm || m_audio_block_samples <= 0)
        return false;

    int bytes = audio_push_pcm_once(m_audio_block_pcm.get(), m_audio_block_samples);
    if (bytes == 0)
        return false;

    Task& t = m_tasks[m_audio_block_task];
    t.regs[2] = (u32)bytes;
    t.blocked = false;
    t.block_audio = false;
    m_audio_block_task = -1;
    m_audio_block_samples = 0;
    m_audio_block_pcm.reset();
    m_audio_unblock_count++;
    return true;
}

bool Syscalls::audio_block_task_for_write(int sample_count) {
    if (m_current_task < 0 || m_current_task >= m_task_count || sample_count <= 0)
        return false;

    u32 return_pc = g_cpu_regs[31];
    if (return_pc < 0x80000000)
        return false;

    if (!m_audio_block_pcm)
        m_audio_block_pcm = std::make_unique<s16[]>(AUDIO_MAX_CHUNK_SAMPLES);
    memcpy(m_audio_block_pcm.get(), m_audio_scratch.get(),
           (size_t)sample_count * sizeof(s16));

    m_audio_block_task = m_current_task;
    m_audio_block_samples = sample_count;

    save_current_task();
    Task& t = m_tasks[m_current_task];
    t.pc = return_pc;
    t.blocked = true;
    t.block_audio = true;
    m_audio_block_count++;

    int next = find_ready_task();
    if (next >= 0) {
        switch_to_task(next);
    } else {
        m_in_idle = true;
        m_current_task = -1;
        g_cpu_pc = m_idle_pc;
        m_task_switched = true;
    }
    return true;
}

void Syscalls::audio_drain_and_close() {
    if (m_audio_device > 0) {
        SDL_PauseAudioDevice(m_audio_device, 0);
        u32 deadline = SDL_GetTicks() + 500;
        while (audio_ring_used() > 0 && SDL_GetTicks() < deadline)
            SDL_Delay(5);
        SDL_PauseAudioDevice(m_audio_device, 1);
        SDL_CloseAudioDevice(m_audio_device);
        m_audio_device = 0;
    }
    m_audio_open = false;
    m_audio_device_open = false;
    m_audio_sem_reg_count = 0;
    audio_reset_ring();
    audio_write_os_state(false);
}

void Syscalls::audio_close_immediate() {
    if (m_audio_device > 0) {
        SDL_PauseAudioDevice(m_audio_device, 1);
        SDL_CloseAudioDevice(m_audio_device);
        m_audio_device = 0;
    }
    m_audio_open = false;
    m_audio_device_open = false;
    if (m_audio_block_task >= 0 && m_audio_block_task < m_task_count) {
        m_tasks[m_audio_block_task].blocked = false;
        m_tasks[m_audio_block_task].block_audio = false;
    }
    m_audio_block_task = -1;
    m_audio_block_samples = 0;
    m_audio_block_pcm.reset();
    m_audio_sem_reg_count = 0;
    audio_reset_ring();
    audio_write_os_state(false);
}

int Syscalls::audio_guest_sample_count(u32 byte_size) const {
    if (m_audio_bits == 8)
        return (int)byte_size;
    return (int)(byte_size / 2);
}

void Syscalls::audio_read_guest_pcm(u32 buf_addr, u32 byte_size, s16* out, int max_samples) const {
    if (m_audio_bits == 8) {
        int count = (int)byte_size;
        if (count > max_samples)
            count = max_samples;
        for (int i = 0; i < count; i++) {
            u8 v = m_mem.read_u8(buf_addr + (u32)i);
            out[i] = (s16)(((int)v - 128) * 256);
        }
    } else {
        int bytes = (int)byte_size;
        if (bytes > max_samples * 2)
            bytes = max_samples * 2;
        m_mem.read_block(buf_addr, (u8*)out, (u32)bytes);
    }
}

int Syscalls::audio_do_write(u32 buf_addr, u32 byte_size) {
    if (byte_size == 0 || !buf_addr)
        return 0;

    int sample_count = audio_guest_sample_count(byte_size);
    if (sample_count <= 0)
        return 0;
    if (sample_count > AUDIO_MAX_CHUNK_SAMPLES) {
        printf("[AUDIO] write too large: %u bytes (%d samples)\n", byte_size, sample_count);
        return 0;
    }
    if (!m_audio_scratch)
        return 0;

    audio_read_guest_pcm(buf_addr, byte_size, m_audio_scratch.get(), sample_count);
    int bytes = audio_push_pcm_once(m_audio_scratch.get(), sample_count);
    if (bytes > 0)
        return bytes;

    if (!m_audio_open)
        return 0;

    // Already waiting for ring space on this task.
    if (m_audio_block_task == m_current_task)
        return -1;

    // Ring full — block this task cooperatively and run other tasks until vsync/callback frees space.
    if (m_current_task >= 0 && m_current_task < m_task_count && m_task_count > 0) {
        if (audio_block_task_for_write(sample_count))
            return -1;
    }

    m_audio_overruns++;
    return 0;
}

void SDLCALL Syscalls::audio_callback(void* userdata, Uint8* stream, int len) {
    Syscalls* sys = static_cast<Syscalls*>(userdata);
    if (!sys || !sys->m_ring_buf || !sys->m_ring_cap) {
        memset(stream, 0, (size_t)len);
        return;
    }

    s16* out = reinterpret_cast<s16*>(stream);
    int want_samples = len / 2;
    int produced = 0;

    if (!sys->m_audio_paused) {
        uint32_t head = sys->m_ring_head.load(std::memory_order_relaxed);
        uint32_t tail = sys->m_ring_tail.load(std::memory_order_acquire);
        uint32_t avail = tail - head;
        int to_read = want_samples;
        if ((uint32_t)to_read > avail)
            to_read = (int)avail;

        float gain = sys->audio_gain();
        if (to_read > 0) {
            uint32_t idx = head & sys->m_ring_mask;
            uint32_t first = std::min((uint32_t)to_read, sys->m_ring_cap - idx);
            if (gain == 1.0f) {
                memcpy(out, &sys->m_ring_buf[idx], first * sizeof(s16));
                if ((uint32_t)to_read > first)
                    memcpy(out + first, &sys->m_ring_buf[0],
                           (uint32_t)(to_read - first) * sizeof(s16));
            } else {
                for (uint32_t i = 0; i < first; i++)
                    out[i] = (s16)(sys->m_ring_buf[idx + i] * gain);
                if ((uint32_t)to_read > first) {
                    for (int i = 0; i < to_read - (int)first; i++)
                        out[first + i] = (s16)(sys->m_ring_buf[i] * gain);
                }
            }
            sys->m_ring_head.store(head + (uint32_t)to_read, std::memory_order_release);
            sys->m_samples_played.fetch_add((uint64_t)to_read, std::memory_order_relaxed);
            produced = to_read;
            sys->m_audio_space_flag.store(true, std::memory_order_relaxed);
        }
    }

    if (produced < want_samples) {
        memset(out + produced, 0, (size_t)(want_samples - produced) * sizeof(s16));
        if (sys->m_audio_has_data && !sys->m_audio_paused)
            sys->m_audio_underruns += (u32)(want_samples - produced);
    }
}


u32 Syscalls::arg(int n) {
    if (n >= 0 && n <= 3) return g_cpu_regs[4 + n];
    // o32 ABI: the caller reserves 16 bytes of shadow space for $a0-$a3, so the
    // 5th argument (n == 4) sits at $sp+16, the 6th at $sp+20, and so on.
    u32 sp = g_cpu_regs[29];
    return m_mem.read_u32(sp + (u32)n * 4);
}

std::string Syscalls::guest_string(u32 vaddr) {
    return m_mem.read_string(vaddr);
}

std::string Syscalls::read_guest_path(u32 vaddr) {
    if (vaddr == 0) return {};
    std::string ascii = guest_string(vaddr);
    if (!ascii.empty() && (unsigned char)ascii[0] >= 0x20)
        return ascii;
    std::string wide;
    for (u32 i = 0; i < 512; i++) {
        u16 c = m_mem.read_u16(vaddr + i * 2);
        if (c == 0) break;
        if (c < 128) wide += (char)c;
        else wide += '?';
    }
    return wide;
}

const char* Syscalls::got_name(int index) const {
    if (index >= 0 && index < (int)m_slot_handlers.size()) {
        int hi = m_slot_handlers[index];
        if (hi >= 0) return s_handlers[hi].name;
    }
    return "unknown";
}

bool Syscalls::got_is_stub(int index) const {
    if (index >= 0 && index < (int)m_slot_handlers.size()) {
        int hi = m_slot_handlers[index];
        if (hi >= 0) return s_handlers[hi].is_stub;
    }
    return true;  // unknown = stub (no implementation available)
}

void Syscalls::dispatch(int got_index, u32 return_addr) {
    (void)return_addr;
    m_got_call_count++;
    if (got_index >= 0 && got_index < (int)MAX_GOT_ENTRIES)
        m_got_call_counts[got_index]++;
    if (got_index >= 0 && got_index < (int)m_slot_handlers.size()) {
        int hi = m_slot_handlers[got_index];
        if (hi >= 0) {
            (this->*s_handlers[hi].handler)();
            return;
        }
    }
    printf("[SYSCALL] Unknown GOT index %d\n", got_index);
    // NOTE: regs[31] is set by dispatch caller from g_cpu_regs after dispatch returns.
    // Do NOT set g_cpu_regs[31] here - it would override task context switches.
}

// === Heap ===

static constexpr u32 HEAP_MAX_SINGLE = 16u * 1024u * 1024u;
// Guest stack lives at phys 0x01FF0000 (64 KB). Never let the bump allocator
// walk into that region — Rubido's titlescreen mapping used to smash $ra.
static constexpr u32 HEAP_LIMIT = 0x01FE0000;

u32 Syscalls::heap_alloc(u32 size) {
    if (size == 0) size = 1;
    size = (size + 7) & ~7;
    if (size > HEAP_MAX_SINGLE) {
        printf("[HEAP] OOM: rejected oversize allocation size=%u PC=0x%08X\n", size, g_cpu_pc);
        return 0;
    }
    u32 top = m_heap_top;
    if (top + size > 0x009FFFFC && top < 0x00C10000)
        top = 0x00C10000;
    u64 new_top = (u64)top + size;
    if (new_top > HEAP_LIMIT) {
        printf("[HEAP] OOM: top=0x%08X size=%u (limit 0x%08X)\n", top, size, HEAP_LIMIT);
        return 0;
    }
    u32 addr = top;
    m_heap_top = (u32)new_top;
    m_heap.push_back({addr, size, false});
    return addr | 0x80000000;  // KSEG0 for TLB bypass
}

void Syscalls::heap_free(u32 addr) {
    if (addr == 0) return;
    u32 phys = addr & 0x1FFFFFFFu;  // strip KSEG0/KSEG1 bit — game holds virt, we store phys
    for (auto& block : m_heap) {
        if (block.addr == phys && !block.free) {
            block.free = true;
            return;
        }
    }
}

u32 Syscalls::heap_realloc(u32 addr, u32 new_size) {
    if (addr == 0) return heap_alloc(new_size);
    if (new_size == 0) { heap_free(addr); return 0; }
    u32 phys = addr & 0x1FFFFFFFu;
    for (auto& block : m_heap) {
        if (block.addr == phys && !block.free) {
            if (new_size <= block.size) return addr;
            u32 new_addr = heap_alloc(new_size);
            if (new_addr) {
                u8* src = &m_mem.get_raw_ptr()[m_mem.vaddr_to_phys(addr)];
                u8* dst = &m_mem.get_raw_ptr()[m_mem.vaddr_to_phys(new_addr)];
                memcpy(dst, src, block.size);
                block.free = true;
            }
            return new_addr;
        }
    }
    return 0;
}

// === File handles ===

int Syscalls::alloc_file_handle() {
    for (int i = 0; i < 64; i++) {
        if (!m_files[i].in_use) {
            m_files[i].in_use = true;
            m_files[i].offset = 0;
            return i;
        }
    }
    return -1;
}

void Syscalls::close_file_handle(int idx) {
    if (idx < 0 || idx >= 64) return;
    if (m_files[idx].is_host && m_files[idx].host_file) {
        fclose(m_files[idx].host_file);
    }
    m_files[idx].in_use = false;
    m_files[idx].is_host = false;
    m_files[idx].is_archive = false;
    m_files[idx].host_file = nullptr;
    m_files[idx].archive = nullptr;
    m_files[idx].archive_entry = nullptr;
    m_files[idx].embedded_data.clear();
    m_files[idx].offset = 0;
}

// === Internal I/O helpers (used by both stdlib-style and fsys_* calls) ===

static bool is_spk_index_bin(const std::string& path) {
    return path.size() >= 4 &&
           (path.compare(path.size() - 4, 4, ".bin") == 0 ||
            path.compare(path.size() - 4, 4, ".BIN") == 0);
}

u32 Syscalls::do_fread(u32 ptr, u32 size, u32 nmemb, u32 file_handle) {
    int idx = (int)file_handle - 1;
    if (idx < 0 || idx >= 64 || !m_files[idx].in_use) return 0;
    u32 total = size * nmemb;
    if (m_files[idx].is_host && m_files[idx].host_file) {
        std::vector<u8> buf(total);
        size_t read = fread(buf.data(), 1, total, m_files[idx].host_file);
        m_mem.write_block(ptr, buf.data(), (u32)read);
        m_files[idx].offset += (u32)read;
        return (u32)(read / size);
    }
    if (m_files[idx].is_archive && m_files[idx].archive && m_files[idx].archive_entry) {
        u32 available = m_files[idx].archive_entry->size - m_files[idx].offset;
        u32 to_read = std::min(total, available);
        if (to_read > 0) {
            const u8* src = m_files[idx].archive->get_data(*m_files[idx].archive_entry)
                            + m_files[idx].offset;
            m_mem.write_block(ptr, src, to_read);
            m_files[idx].offset += to_read;
        }
        return size ? to_read / size : 0;
    }
    {
        u32 available = (u32)m_files[idx].embedded_data.size() - m_files[idx].offset;
        u32 to_read = std::min(total, available);
        if (to_read > 0) {
            m_mem.write_block(ptr, &m_files[idx].embedded_data[m_files[idx].offset], to_read);
            m_files[idx].offset += to_read;
        }
        return to_read / size;
    }
}

u32 Syscalls::do_fwrite(u32 ptr, u32 size, u32 nmemb, u32 file_handle) {
    int idx = (int)file_handle - 1;
    if (idx < 0 || idx >= 64 || !m_files[idx].in_use) return 0;
    u32 total = size * nmemb;
    if (m_files[idx].is_host && m_files[idx].host_file) {
        std::vector<u8> buf(total);
        m_mem.read_block(ptr, buf.data(), total);
        size_t written = fwrite(buf.data(), 1, total, m_files[idx].host_file);
        m_files[idx].offset += (u32)written;
        return (u32)(written / size);
    }
    return 0;
}

u32 Syscalls::do_fseek(u32 file_handle, s32 offset, u32 whence) {
    int idx = (int)file_handle - 1;
    if (idx < 0 || idx >= 64 || !m_files[idx].in_use) return (u32)-1;
    if (m_files[idx].is_host && m_files[idx].host_file) {
        u32 ret = (u32)fseek(m_files[idx].host_file, offset, (int)whence);
        if (ret == 0) m_files[idx].offset = (u32)ftell(m_files[idx].host_file);
        return ret;
    }
    if (m_files[idx].is_archive && m_files[idx].archive_entry) {
        u32 sz = m_files[idx].archive_entry->size;
        if (whence == 0) m_files[idx].offset = (u32)offset;
        else if (whence == 1) m_files[idx].offset = (u32)((s32)m_files[idx].offset + offset);
        else if (whence == 2) m_files[idx].offset = (u32)((s32)sz + offset);
        if (m_files[idx].offset > sz) m_files[idx].offset = sz;
        return 0;
    }
    {
        if (whence == 0) m_files[idx].offset = (u32)offset;
        else if (whence == 1) m_files[idx].offset += (u32)offset;
        else if (whence == 2) return (u32)m_files[idx].embedded_data.size() + (u32)offset;
        return m_files[idx].offset;
    }
}

u32 Syscalls::do_ftell(u32 file_handle) {
    int idx = (int)file_handle - 1;
    if (idx < 0 || idx >= 64 || !m_files[idx].in_use) return (u32)-1;
    if (m_files[idx].is_host && m_files[idx].host_file)
        return (u32)ftell(m_files[idx].host_file);
    return m_files[idx].offset;
}

u32 Syscalls::do_feof(u32 file_handle) {
    int idx = (int)file_handle - 1;
    if (idx < 0 || idx >= 64 || !m_files[idx].in_use) return 1;
    if (m_files[idx].is_host && m_files[idx].host_file)
        return (u32)feof(m_files[idx].host_file);
    if (m_files[idx].is_archive && m_files[idx].archive_entry)
        return m_files[idx].offset >= m_files[idx].archive_entry->size ? 1 : 0;
    return m_files[idx].offset >= m_files[idx].embedded_data.size() ? 1 : 0;
}

u32 Syscalls::do_ferror(u32 file_handle) {
    int idx = (int)file_handle - 1;
    if (idx < 0 || idx >= 64 || !m_files[idx].in_use) return 0;
    if (m_files[idx].is_host && m_files[idx].host_file)
        return (u32)ferror(m_files[idx].host_file);
    return 0;
}

// === Format string helper ===

std::string Syscalls::format_string(const std::string& fmt, int first_arg) {
    std::string result;
    int arg_idx = first_arg;

    for (size_t i = 0; i < fmt.size(); ) {
        if (fmt[i] != '%') { result += fmt[i++]; continue; }
        size_t spec_start = i++;
        if (i >= fmt.size()) { result += '%'; break; }
        if (fmt[i] == '%') { result += '%'; i++; continue; }

        // Collect flags
        while (i < fmt.size() && (fmt[i] == '-' || fmt[i] == '+' ||
               fmt[i] == ' ' || fmt[i] == '#' || fmt[i] == '0')) i++;
        // Width
        while (i < fmt.size() && fmt[i] >= '0' && fmt[i] <= '9') i++;
        // Precision
        if (i < fmt.size() && fmt[i] == '.') {
            i++;
            while (i < fmt.size() && fmt[i] >= '0' && fmt[i] <= '9') i++;
        }
        // Strip length modifiers (all values are 32-bit in guest)
        while (i < fmt.size() && (fmt[i] == 'l' || fmt[i] == 'h' ||
               fmt[i] == 'z' || fmt[i] == 'L')) i++;

        if (i >= fmt.size()) break;
        char conv = fmt[i++];

        // Rebuild clean spec string (no length modifier)
        std::string spec;
        for (size_t j = spec_start; j < i - 1; j++) {
            char c = fmt[j];
            if (c != 'l' && c != 'h' && c != 'z' && c != 'L') spec += c;
        }
        spec += conv;

        char buf[512];
        switch (conv) {
        case 'd': case 'i':
            snprintf(buf, sizeof(buf), spec.c_str(), (int)(s32)arg(arg_idx++));
            result += buf; break;
        case 'u':
            snprintf(buf, sizeof(buf), spec.c_str(), (unsigned)arg(arg_idx++));
            result += buf; break;
        case 'x': case 'X': case 'o':
            snprintf(buf, sizeof(buf), spec.c_str(), (unsigned)arg(arg_idx++));
            result += buf; break;
        case 'p':
            snprintf(buf, sizeof(buf), "%08x", arg(arg_idx++));
            result += buf; break;
        case 's': {
            u32 saddr = arg(arg_idx++);
            std::string s = (saddr != 0) ? guest_string(saddr) : "(null)";
            snprintf(buf, sizeof(buf), spec.c_str(), s.c_str());
            result += buf; break;
        }
        case 'c':
            result += (char)(arg(arg_idx++) & 0xFF); break;
        case 'f': case 'g': case 'e': case 'E': case 'G':
            snprintf(buf, sizeof(buf), spec.c_str(), 0.0);
            result += buf; arg_idx++; break;
        default:
            // Unknown: emit literal spec text
            result += fmt.substr(spec_start, i - spec_start);
            break;
        }
    }
    return result;
}

// === GOT 0-10: libc ===

void Syscalls::impl_abort() {
    printf("[ABORT] abort() called from PC=0x%08X (returning as no-op)\n", g_cpu_pc);
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_printf() {
    std::string out = format_string(guest_string(arg(0)), 1);
    fputs(out.c_str(), stdout);
    fflush(stdout);
    g_cpu_regs[2] = (u32)out.size();
}

void Syscalls::impl_sprintf() {
    u32 buf_addr = arg(0);
    std::string result = format_string(guest_string(arg(1)), 2);
    m_mem.write_block(buf_addr, (const u8*)result.c_str(), (u32)result.size() + 1);
    g_cpu_regs[2] = (u32)result.size();
}

void Syscalls::impl_fprintf() {
    // arg(0) = FILE*, arg(1) = format string, arg(2+) = varargs
    // FILE* is ignored — all guest output goes to host stdout.
    std::string out = format_string(guest_string(arg(1)), 2);
    fputs(out.c_str(), stdout);
    fflush(stdout);
    g_cpu_regs[2] = (u32)out.size();
}

void Syscalls::impl_strncasecmp() {
    u32 a_addr = arg(0);
    u32 b_addr = arg(1);
    u32 n = arg(2);
    std::string a = guest_string(a_addr);
    std::string b = guest_string(b_addr);
    size_t len = std::min((size_t)n, std::min(a.size(), b.size()));
    int result = 0;
    for (size_t i = 0; i < len; i++) {
        int ca = tolower((unsigned char)a[i]);
        int cb = tolower((unsigned char)b[i]);
        if (ca != cb) { result = ca - cb; break; }
    }
    if (result == 0 && a.size() != b.size())
        result = (a.size() < b.size()) ? -1 : 1;
    g_cpu_regs[2] = (u32)result;
}

void Syscalls::impl_malloc() {
    g_cpu_regs[2] = heap_alloc(arg(0));
}

void Syscalls::impl_realloc() {
    g_cpu_regs[2] = heap_realloc(arg(0), arg(1));
}

void Syscalls::impl_free() {
    heap_free(arg(0));
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_fread() {
    u32 ptr = arg(0);
    u32 size = arg(1);
    u32 nmemb = arg(2);
    u32 file_handle = arg(3);
    g_cpu_regs[2] = do_fread(ptr, size, nmemb, file_handle);
}

void Syscalls::impl_fwrite() {
    u32 ptr = arg(0);
    u32 size = arg(1);
    u32 nmemb = arg(2);
    u32 file_handle = arg(3);
    g_cpu_regs[2] = do_fwrite(ptr, size, nmemb, file_handle);
}

void Syscalls::impl_fseek() {
    u32 file_handle = arg(0);
    s32 offset = (s32)arg(1);
    u32 whence = arg(2);
    g_cpu_regs[2] = do_fseek(file_handle, offset, whence);
}

// === GOT 11-23: display / cache ===

void Syscalls::impl_LcdGetDisMode() {
    g_cpu_regs[2] = 1;
}

void Syscalls::impl_vxGoHome() {
    printf("[STUB] vxGoHome\n");
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_StartSwTimer() {
    u32 period = arg(0);
    u32 callback = arg(1);
    printf("[TIMER] StartSwTimer(period=%u, callback=0x%08X)\n", period, callback);

    // Find free slot
    int idx = -1;
    for (size_t i = 0; i < m_timers.size(); i++) {
        if (!m_timers[i].active) { idx = (int)i; break; }
    }
    if (idx < 0) {
        idx = (int)m_timers.size();
        m_timers.push_back({});
    }
    m_timers[idx].active = true;
    m_timers[idx].period_ms = period;
    m_timers[idx].callback = callback;
    m_timers[idx].elapsed = 0;
    g_cpu_regs[2] = (u32)(idx + 1); // return 1-based timer handle
}

void Syscalls::process_timers() {
    if (m_timers.size() > 64) {
        printf("[TIMER] ignoring corrupt list size=%zu\n", m_timers.size());
        fflush(stdout);
        return;
    }
    u32 now = SDL_GetTicks();
    u32 delta = now - m_last_timer_tick;
    m_last_timer_tick = now;

    if (delta == 0) return;

    for (auto& t : m_timers) {
        if (!t.active || t.callback == 0 || t.period_ms == 0) continue;
        t.elapsed += delta;
        u32 steps = 0;
        while (t.elapsed >= t.period_ms && steps++ < 8) {
            t.elapsed -= t.period_ms;
            printf("[TIMER] Firing timer callback 0x%08X\n", t.callback);
            call_guest_function(t.callback, 0);
        }
    }
}

void Syscalls::call_guest_function(u32 func_addr, u32 arg0) {
    // Save current context
    u32 old_ra = g_cpu_regs[31];
    u32 old_sp = g_cpu_regs[29];

    // Allocate stack frame: push original ra for the return stub
    u32 sp = old_sp - 16;
    m_mem.write_u32(sp + 0, old_ra);
    m_mem.write_u32(sp + 4, 0); // padding

    // Set up to call the function
    g_cpu_regs[29] = sp;
    g_cpu_regs[31] = 0x80BFFF00; // return stub restores ra and sp
    g_cpu_regs[4] = arg0;       // a0 = argument
    g_cpu_pc = func_addr;
}

void Syscalls::impl_free_irq() {
    printf("[STUB] free_irq\n");
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_fsys_RefreshCache() {
    printf("[STUB] fsys_RefreshCache\n");
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_strlen() {
    g_cpu_regs[2] = (u32)guest_string(arg(0)).size();
}

// Frame buffer pool management
u32 Syscalls::allocate_fb(u32 size) {
    // First try an unused slot with enough capacity
    for (auto& e : m_fb_pool) {
        if (!e.in_use && e.size >= size) {
            e.in_use = true;
            return e.phys;
        }
    }
    // Next try an unused slot that can be reallocated
    for (auto& e : m_fb_pool) {
        if (!e.in_use) {
            e.phys = heap_alloc(size) & 0x1FFFFFFF;
            e.size = size;
            e.in_use = true;
            return e.phys;
        }
    }
    // Pool full: reuse the least-recently-used slot (index 0)
    if (m_fb_pool[0].in_use) {
        // Don't free the old one — just leak it and overwrite
        m_fb_pool[0].in_use = false;
    }
    // Retry now that slot 0 is freed
    return allocate_fb(size);
}

void Syscalls::release_fb(u32 phys) {
    for (auto& e : m_fb_pool) {
        if (e.phys == phys && e.in_use) {
            e.in_use = false;
            return;
        }
    }
}

// Heuristic: sample 10 pixels and check byte[3] (alpha in LE word [B,G,R,A]).
// If >= 8/10 have alpha == 0x00 or 0xFF → ARGB8888 (4 bytes/pixel).
// Otherwise → RGB565 (2 bytes/pixel). Return pixel size (2 or 4).

void Syscalls::argb8888_to_rgb565(const u8* src, u8* dst, u32 pixel_count) {
    for (u32 i = 0; i < pixel_count; i++) {
        u8 b = src[i * 4 + 0];
        u8 g = src[i * 4 + 1];
        u8 r = src[i * 4 + 2];
        u16 rgb565 = (u16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        dst[i * 2 + 0] = (u8)(rgb565 & 0xFF);
        dst[i * 2 + 1] = (u8)(rgb565 >> 8);
    }
}

void Syscalls::impl__lcd_set_frame() {
    // Zero-arg OS API: flip the buffer last handed out by _lcd_get_frame ($a0 ignored).
    static constexpr u32 PIXEL_COUNT = Display::WIDTH * Display::HEIGHT;  // 76800

    u32 bpp = m_lcd_bpp;
    u32 buf_size = PIXEL_COUNT * bpp;
    g_cpu_regs[2] = 0;

    u32 start = m_lcd_pending_buf;
    if (!start)
        return;
    u8* ram = m_mem.get_raw_ptr();

    // Count non-zero pixels to check if anything is actually drawn
    static u32 lcd_sample_count = 0;
    if (lcd_sample_count < 10) {
        u32 nonzero = 0, nonwhite = 0;
        u32 stride = bpp;
        for (u32 i = 0; i + stride <= buf_size; i += stride) {
            u32 px = 0;
            for (u32 b = 0; b < stride; b++) px |= ((u32)ram[start + i + b] << (b * 8));
            if (px != 0) nonzero++;
            u32 white = (stride == 2) ? 0xFFFF : 0xFFFFFFFF;
            if (px != white) nonwhite++;
        }
        printf("[LCD] frame %u: start=0x%08X bpp=%u nonzero=%u nonwhite=%u (of %u)\n",
               lcd_sample_count, start, bpp, nonzero, nonwhite, PIXEL_COUNT);
        lcd_sample_count++;
    }

    u32 display_addr = 0;

    if (bpp == 1) {
        // 8-bit indexed → CLUT lookup, convert to RGB565
        u32 buf_phys = allocate_fb(PIXEL_COUNT * 2);
        if (buf_phys && buf_phys + PIXEL_COUNT * 2 <= m_mem.size()) {
            u16* dst = (u16*)(ram + buf_phys);
            for (u32 i = 0; i < PIXEL_COUNT; i++) {
                u8 idx = ram[start + i];
                // CLUT at phys 0x03050100 (KSEG1 0xB3050100): 256 × 32-bit ARGB entries
                u32 clut_entry = 0;
                u32 clut_phys = 0x03050100 + idx * 4;
                if (clut_phys + 3 < m_mem.size()) {
                    clut_entry = *(u32*)(ram + clut_phys);
                }
                u8 r = (u8)(clut_entry >> 16);
                u8 g = (u8)(clut_entry >> 8);
                u8 b = (u8)(clut_entry);
                dst[i] = (u16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            }
            display_addr = buf_phys;
        }
    } else if (bpp == 4) {
        // ARGB8888 → convert to RGB565
        u32 buf_phys = allocate_fb(PIXEL_COUNT * 2);
        if (buf_phys && buf_phys + PIXEL_COUNT * 2 <= m_mem.size()) {
            argb8888_to_rgb565(ram + start, ram + buf_phys, PIXEL_COUNT);
            display_addr = buf_phys;
        } else {
            display_addr = start;
        }
    } else {
        // RGB565 or other direct 2-byte format — use directly
        display_addr = start;
    }

    if (display_addr) {
        u32 old_front = m_display.get_frame_addr();
        m_display.set_frame_addr(display_addr);
        if (old_front != display_addr && old_front != 0)
            m_display.set_back_addr(old_front);
        m_display.flip(ram, m_mem.size());

        // Advance which buffer is "back" so _lcd_get_frame alternates correctly.
        m_lcd_back = !m_lcd_back;
    }
}

void Syscalls::impl__lcd_get_frame() {
    // Allocate two HW frame buffers on first call (each buf_size bytes).
    // Returns the start of whichever buffer is currently the back buffer.
    static constexpr u32 PIXEL_COUNT  = Display::WIDTH * Display::HEIGHT;
    static constexpr u32 BUF_SIZE_RGB = PIXEL_COUNT * 2;   // 153 600 bytes (RGB565)

    if (!m_lcd_hw_buf[0]) {
        u32 pa = heap_alloc(BUF_SIZE_RGB) & 0x1FFFFFFF;
        u32 pb = heap_alloc(BUF_SIZE_RGB) & 0x1FFFFFFF;
        m_lcd_hw_buf[0] = pa;
        m_lcd_hw_buf[1] = pb;
        printf("[LCD] HW frame buffers: buf0=0x%08X buf1=0x%08X\n",
               m_lcd_hw_buf[0], m_lcd_hw_buf[1]);
    }

    u32 back = m_lcd_hw_buf[m_lcd_back ? 1 : 0];
    m_lcd_pending_buf = back;
    g_cpu_regs[2] = back ? (back | 0xA0000000u) : 0;
}

void Syscalls::impl_lcd_get_cframe() {
    u32 front = m_display.get_frame_addr();
    g_cpu_regs[2] = front ? (front | 0x80000000u) : 0;
}

void Syscalls::impl_ap_lcd_set_frame() {
    m_display.set_frame_addr(arg(0) & 0x1FFFFFFF);
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_lcd_flip() {
    m_display.flip(m_mem.get_raw_ptr(), m_mem.size());
    g_cpu_regs[2] = 0;
}

void Syscalls::impl___icache_invalidate_all() {
    printf("[STUB] __icache_invalidate_all\n");
    g_cpu_regs[2] = 0;
}

void Syscalls::impl___dcache_writeback_all() {
    printf("[STUB] __dcache_writeback_all\n");
    g_cpu_regs[2] = 0;
}

// === GOT 24-31: media / OS / serial / input ===

void Syscalls::impl_TaskMediaFunStop() {
    printf("[STUB] TaskMediaFunStop\n");
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_OSCPUSaveSR() {
    u32 old_sr = m_cop0 ? m_cop0->regs.status : 0;
    u32 new_sr = old_sr & ~1u; // clear IE bit (SR bit 0) — disable interrupts
    if (m_cop0) m_cop0->regs.status = new_sr;
    g_cpu_regs[2] = old_sr;
}

void Syscalls::impl_OSCPURestoreSR() {
    u32 sr = arg(0);
    if (m_cop0) m_cop0->regs.status = sr;
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_serial_getc() {
    printf("[STUB] serial_getc\n");
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_serial_putc() {
    putchar((char)arg(0));
    fflush(stdout);
    g_cpu_regs[2] = 0;
}

u32 Syscalls::bitmask_to_keycode(u32 bitmask) {
    return Display::bitmask_to_keycode(bitmask);
}

// key_input_handler in 7days.app checks state_ptr+8 using raw hardware bit positions,
// NOT the DKEY_ bitmask format. Confirmed from disassembly of 0x80A000FC:
//   bit 16 (LUI 0x0001) -> keycode 17 (UP nav)
//   bit 21 (LUI 0x0020) -> keycode 18 (DOWN nav)
void Syscalls::impl__kbd_get_status() {
    u32 state_ptr = arg(0);
    u32 keys = m_display.get_dingoo_keys();  // DKEY_* bitmask for kernel state + $v0
    u32 hw   = m_display.get_hw_keys();      // game-correct bitmask for KEY_STATUS
    u32 hw_pressed  = hw & ~m_prev_hw;       // new hw bits since last call
    u32 hw_released = m_prev_hw & ~hw;        // hw bits released since last call

    if (state_ptr) {
        // KEY_STATUS struct (dingoo_sdk keyboard.h / entry.h):
        //   +0: unsigned long pressed   (keys newly pressed, game-correct bit positions)
        //   +4: unsigned long released  (keys newly released, game-correct bit positions)
        //   +8: unsigned long status    (current key state, game-correct bit positions)
        m_mem.write_u32(state_ptr + 0, hw_pressed);
        m_mem.write_u32(state_ptr + 4, hw_released);
        m_mem.write_u32(state_ptr + 8, hw);
    }
    m_prev_hw = hw;

    g_cpu_regs[2] = keys;

    // Fixed kernel mailbox: games read KEY_STATUS+8 using hw bit positions, not DKEY_*.
    m_mem.write_u32(KERNEL_KEY_STATE_ADDR, hw);
}

void Syscalls::impl_get_game_vol() {
    g_cpu_regs[2] = 80;
}

void Syscalls::impl__kbd_get_key() {
    // Return the key event (type<<8|code) from the event FIFO, matching the
    // real firmware at 0x80169200.  Event types: 0x01=down, 0x02=up, 0x03=held.
    // Returns 0 when the queue is empty.
    if (m_display.has_key_event()) {
        u32 ev = m_display.pop_key_event();
        g_cpu_regs[2] = ev;
        return;
    }
    g_cpu_regs[2] = 0;
}

// === GOT 32-45: filesystem ===

void Syscalls::impl_fsys_fopen() {
    u32 path_addr = arg(0);
    u32 mode_addr = arg(1);
    std::string path = guest_string(path_addr);
    std::string mode = guest_string(mode_addr);

    if (m_archive && !is_spk_index_bin(path)) {
        const ArchiveEntry* entry = m_archive->find(path);
        if (entry) {
            int idx = alloc_file_handle();
            if (idx < 0) { g_cpu_regs[2] = 0; return; }
            m_files[idx].is_archive = true;
            m_files[idx].archive = m_archive;
            m_files[idx].archive_entry = entry;
            m_files[idx].offset = 0;
            g_cpu_regs[2] = (u32)idx + 1;
            return;
        }
    }

    // Try host file for write/append mode
    int idx = alloc_file_handle();
    if (idx < 0) { g_cpu_regs[2] = 0; return; }
    if (mode.find('w') != std::string::npos || mode.find('+') != std::string::npos ||
        mode.find('a') != std::string::npos) {
#ifdef _WIN32
        _mkdir("save");
#else
        mkdir("save", 0755);
#endif
        std::string host_path = "save/" + path;
        FILE* f = fopen(host_path.c_str(), mode.c_str());
        if (f) {
            m_files[idx].is_host = true;
            m_files[idx].host_file = f;
            g_cpu_regs[2] = (u32)idx + 1;
            return;
        }
    }

    m_files[idx].in_use = false;
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_fsys_fread() {
    u32 buf    = arg(0);
    u32 size   = arg(1);
    u32 nmemb  = arg(2);
    u32 handle = arg(3);
    int hidx = (int)handle - 1;
    u32 before = (hidx >= 0 && hidx < 64 && m_files[hidx].in_use) ? m_files[hidx].offset : 0;
    u32 n = do_fread(buf, size, nmemb, handle);
    // Log: show offset-before-read, total bytes requested, first 4 magic bytes of result
    u32 total = size * nmemb;
    u8 magic[4] = {};
    m_mem.read_block(buf, magic, std::min(total, 4u));
    printf("[FREAD] handle=%u off=0x%08X size=%u*%u=%u -> %u items  dest=0x%08X  magic=%02X%02X%02X%02X\n",
           handle, before, size, nmemb, total, n, buf,
           magic[0], magic[1], magic[2], magic[3]);
    g_cpu_regs[2] = n;
}

void Syscalls::impl_fsys_fclose() {
    u32 handle = arg(0);
    printf("[FCLOSE] handle=%u\n", handle);
    close_file_handle((int)handle - 1);
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_fsys_fseek() {
    u32 handle = arg(0);
    s32 offset = (s32)arg(1);
    u32 whence = arg(2);
    static const char* whence_name[] = {"SET","CUR","END"};
    printf("[FSEEK] handle=%u offset=0x%08X (%d) whence=%s\n",
           handle, (u32)offset, offset, whence < 3 ? whence_name[whence] : "?");
    u32 ret = do_fseek(handle, offset, whence);
    g_cpu_regs[2] = ret;
}

void Syscalls::impl_fsys_ftell() {
    g_cpu_regs[2] = do_ftell(arg(0));
}

void Syscalls::impl_fsys_remove() {
    u32 path_addr = arg(0);
    if (!path_addr) { g_cpu_regs[2] = (u32)-1; return; }
    std::string path = guest_string(path_addr);
    printf("[FSYS] remove('%s')\n", path.c_str());

    // Try save/ prefix first (games write to save/ directory)
    std::string host_path = "save/" + path;
    if (remove(host_path.c_str()) == 0) { g_cpu_regs[2] = 0; return; }

    // Fallback: try path as-is
    if (remove(path.c_str()) == 0) { g_cpu_regs[2] = 0; return; }

    g_cpu_regs[2] = (u32)-1;
}

void Syscalls::impl_fsys_rename() {
    u32 old_addr = arg(0);
    u32 new_addr = arg(1);
    if (!old_addr || !new_addr) { g_cpu_regs[2] = (u32)-1; return; }
    std::string old_path = guest_string(old_addr);
    std::string new_path = guest_string(new_addr);
    printf("[FSYS] rename('%s' -> '%s')\n", old_path.c_str(), new_path.c_str());

    // Try save/ prefix first, then fallback to as-is
    std::string host_old = "save/" + old_path;
    std::string host_new = "save/" + new_path;
    if (rename(host_old.c_str(), host_new.c_str()) == 0) { g_cpu_regs[2] = 0; return; }
    if (rename(old_path.c_str(), new_path.c_str()) == 0) { g_cpu_regs[2] = 0; return; }

    g_cpu_regs[2] = (u32)-1;
}

void Syscalls::impl_fsys_ferror() {
    g_cpu_regs[2] = do_ferror(arg(0));
}

void Syscalls::impl_fsys_feof() {
    g_cpu_regs[2] = do_feof(arg(0));
}

void Syscalls::impl_fsys_fwrite() {
    u32 buf    = arg(0);
    u32 size   = arg(1);
    u32 nmemb  = arg(2);
    u32 handle = arg(3);
    g_cpu_regs[2] = do_fwrite(buf, size, nmemb, handle);
}

int Syscalls::alloc_search_handle() {
    for (size_t i = 0; i < m_searches.size(); i++) {
        if (!m_searches[i].in_use) { m_searches[i].in_use = true; return (int)i; }
    }
    int idx = (int)m_searches.size();
    m_searches.push_back({});
    m_searches[idx].in_use = true;
    return idx;
}

void Syscalls::free_search_handle(int idx) {
    if (idx < 0 || idx >= (int)m_searches.size() || !m_searches[idx].in_use) return;
    if (m_searches[idx].dir) closedir(m_searches[idx].dir);
    m_searches[idx].in_use = false;
    m_searches[idx].dir = nullptr;
}

void Syscalls::impl_fsys_findfirst() {
    u32 path_addr = arg(0);
    int filter = (int)arg(1);   // e.g. -1, FSYS_FIND_FILE(0), FSYS_FIND_DIRECTORY(0x10)
    u32 info_addr = arg(2);

    std::string path = path_addr ? guest_string(path_addr) : "";
    printf("[FSYS] findfirst(path='%s', filter=%d, info=0x%08X)\n", path.c_str(), filter, info_addr);

    if (!info_addr) { g_cpu_regs[2] = (u32)-1; return; }

    // Normalise path: strip 'a:\' / '.\' and keep directory part
    std::string dir_path;
    {
        size_t star = path.find('*');
        if (star != std::string::npos) path = path.substr(0, star);
        size_t last_slash = path.find_last_of("/\\");
        dir_path = (last_slash != std::string::npos) ? path.substr(0, last_slash) : ".";
        // Strip leading .\ ./
        while (dir_path.size() >= 2 && dir_path[0] == '.' && (dir_path[1] == '\\' || dir_path[1] == '/'))
            dir_path = dir_path.substr(2);
        // Strip leading "a:" or "A:"
        if (dir_path.size() >= 2 && dir_path[1] == ':')
            dir_path = dir_path.substr(2);
        if (dir_path.empty()) dir_path = ".";
        // Append backslash for opendir safety
        if (dir_path.back() != '/' && dir_path.back() != '\\')
            dir_path += '/';
    }

    int idx = alloc_search_handle();
    m_searches[idx].dir = opendir(dir_path.c_str());
    m_searches[idx].filter = filter;
    m_searches[idx].dir_path = dir_path;

    if (!m_searches[idx].dir) {
        // Fallback: try save/ prefix
        std::string alt = "save/" + dir_path;
        m_searches[idx].dir = opendir(alt.c_str());
        if (m_searches[idx].dir) m_searches[idx].dir_path = alt;
    }
    if (!m_searches[idx].dir) {
        printf("[FSYS] findfirst: cannot open '%s'\n", dir_path.c_str());
        free_search_handle(idx);
        g_cpu_regs[2] = (u32)-1;
        return;
    }

    // Store handle in user struct (1‑based)
    u32 info_phys = info_addr & 0x1FFFFFFF;
    m_mem.write_u32(info_phys + 0, (u32)(idx + 1)); // handle

    // Fill first entry
    g_cpu_regs[2] = 0; // assume success
    impl_fsys_findnext(); // sets result
    // If next returns -1, first also fails
    u32 result = g_cpu_regs[2];
    if (result != 0) {
        // No entries found – close and return -1
        free_search_handle(idx);
        m_mem.write_u32(info_phys + 0, 0);
    }
    g_cpu_regs[2] = result;
}

void Syscalls::impl_fsys_findnext() {
    u32 info_addr = arg(0);
    if (!info_addr) { g_cpu_regs[2] = (u32)-1; return; }

    u32 info_phys = info_addr & 0x1FFFFFFF;
    u32 handle = m_mem.read_u32(info_phys + 0);
    if (handle == 0) { g_cpu_regs[2] = (u32)-1; return; }
    int idx = (int)handle - 1;
    if (idx < 0 || idx >= (int)m_searches.size() || !m_searches[idx].in_use || !m_searches[idx].dir) {
        g_cpu_regs[2] = (u32)-1;
        return;
    }

    DIR* dir = m_searches[idx].dir;
    int filter = m_searches[idx].filter;
    const std::string& base_path = m_searches[idx].dir_path;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        // Build full path for stat
        std::string full = base_path + entry->d_name;
        struct stat st;
        if (stat(full.c_str(), &st) != 0) continue;

        // Determine attributes
        u32 attr = 0;
        if (S_ISDIR(st.st_mode))
            attr = FSYS_ATTR_DIR;
        else if (S_ISREG(st.st_mode))
            attr = FSYS_ATTR_FILE;
        else
            continue; // skip unexpected

        // Apply filter
        if (filter != -1) {
            if (filter == FSYS_FIND_FILE && (attr & FSYS_ATTR_FILE) == 0) continue;
            if (filter == FSYS_FIND_DIRECTORY && (attr & FSYS_ATTR_DIR) == 0) continue;
        }

        // Fill info struct (offset layout: handle=0, size=4, attr=8, time=12, pad=16, name=18)
        m_mem.write_u32(info_phys + 4, (u32)st.st_size);
        m_mem.write_u32(info_phys + 8, attr);
        m_mem.write_u32(info_phys + 12, 0); // time
        m_mem.write_u16(info_phys + 16, 0); // padding
        size_t name_len = strlen(entry->d_name);
        if (name_len > FSYS_FILENAME_MAX - 1) name_len = FSYS_FILENAME_MAX - 1;
        for (size_t i = 0; i < name_len; i++)
            m_mem.write_u8(info_phys + 18 + (u32)i, (u8)entry->d_name[i]);
        m_mem.write_u8(info_phys + 18 + (u32)name_len, 0);

        g_cpu_regs[2] = 0; // success
        return;
    }
    g_cpu_regs[2] = (u32)-1; // no more entries
}

void Syscalls::impl_fsys_findclose() {
    u32 info_addr = arg(0);
    if (!info_addr) { g_cpu_regs[2] = (u32)-1; return; }
    u32 info_phys = info_addr & 0x1FFFFFFF;
    u32 handle = m_mem.read_u32(info_phys + 0);
    if (handle == 0) { g_cpu_regs[2] = 0; return; }
    int idx = (int)handle - 1;
    free_search_handle(idx);
    m_mem.write_u32(info_phys + 0, 0);
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_fsys_flush_cache() {
    printf("[STUB] fsys_flush_cache\n");
    g_cpu_regs[2] = 0;
}

// === GOT 46-48: USB ===

void Syscalls::impl_USB_Connect() {
    printf("[STUB] USB_Connect\n");
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_udc_attached() {
    printf("[STUB] udc_attached\n");
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_USB_No_Connect() {
    printf("[STUB] USB_No_Connect\n");
    g_cpu_regs[2] = 0;
}

// === GOT 49-57: audio ===

void Syscalls::audio_set_volume_level(u32 vol) {
    if (vol > 100)
        vol = 100;
    m_volume_level = (u8)vol;
    if (vol > 30)
        m_volume = (float)vol / 100.0f;
    else
        m_volume = (float)vol / 30.0f;
}

void Syscalls::impl_waveout_open() {
    if (m_nosound) { g_cpu_regs[2] = 1; return; }
    u32 a0 = arg(0), a1 = arg(1), a2 = arg(2);
    printf("[AUDIO] waveout_open raw args: a0=0x%08X a1=0x%08X a2=0x%08X\n", a0, a1, a2);

    int sample_rate, channels, bits;
    u16 format = 16;
    u8  volume = 100;

    if (a0 >= 0x80000000 || (a0 >= 0x1000 && a0 < m_mem.size())) {
        sample_rate = (int)m_mem.read_u32(a0);
        format      = m_mem.read_u16(a0 + 4);
        channels    = (int)m_mem.read_u8(a0 + 6);
        volume      = m_mem.read_u8(a0 + 7);
        printf("[AUDIO] struct@0x%08X: rate=%d format=%u ch=%u vol=%u\n",
               a0, sample_rate, format, channels, volume);
    } else {
        sample_rate = (int)a0;
        channels    = (int)a1;
        bits        = (int)a2;
        format      = (bits == 8) ? 8 : 16;
    }

    if (sample_rate <= 0) sample_rate = 44100;
    if (channels <= 0 || channels > 2) channels = 2;
    bits = (format == 8) ? 8 : 16;
    m_audio_bits = (u32)bits;

    audio_set_volume_level(volume);

    if (!SDL_WasInit(SDL_INIT_AUDIO))
        SDL_InitSubSystem(SDL_INIT_AUDIO);

    m_audio_sample_rate = (u32)sample_rate;
    m_audio_channels    = (u32)channels;
    audio_alloc_ring(m_audio_sample_rate, m_audio_channels);

    m_audio_paused = false;
    m_audio_muted  = false;
    m_audio_has_data = false;
    m_samples_played.store(0, std::memory_order_relaxed);
    m_samples_written.store(0, std::memory_order_relaxed);
    m_audio_post_watermark = 0;
    m_audio_hw_chunk_bytes = 800;
    m_audio_start_tick = SDL_GetTicks();

    if (!audio_open_device(sample_rate, channels)) {
        printf("[AUDIO] waveout_open FAILED: %s\n", SDL_GetError());
        m_audio_open = false;
        m_audio_device_open = false;
        g_cpu_regs[2] = 0;
        return;
    }

    m_audio_open = true;
    m_audio_device_open = true;
    audio_write_os_state(true);
    printf("[AUDIO] waveout_open: %dHz %dch %dbit -> device=%u\n",
           sample_rate, channels, bits, m_audio_device);
    g_cpu_regs[2] = 1;
}

void Syscalls::impl_waveout_close() {
    if (m_nosound) { g_cpu_regs[2] = 0; return; }
    audio_drain_and_close();
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_waveout_close_at_once() {
    if (m_nosound) { g_cpu_regs[2] = 0; return; }
    audio_close_immediate();
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_waveout_reset() {
    if (m_nosound) { g_cpu_regs[2] = 0; return; }
    audio_reset_ring();
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_waveout_set_volume() {
    if (m_nosound) { g_cpu_regs[2] = 0; return; }
    u32 vol = arg(0);
    if (vol > 100)
        vol = arg(1);
    audio_set_volume_level(vol);
    printf("[AUDIO] waveout_set_volume(%u) -> %.2f\n", vol, m_volume);
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_waveout_get_volume() {
    g_cpu_regs[2] = m_volume_level;
}

void Syscalls::impl_HP_Mute_sw() {
    if (m_nosound) { g_cpu_regs[2] = 0; return; }
    printf("[AUDIO] HP_Mute_sw -> muted\n");
    m_audio_muted = true;
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_waveout_write() {
    u32 buf_addr = arg(1);
    u32 size = arg(2);

    if (m_nosound) { g_cpu_regs[2] = size; return; }

    if (m_audio_write_count < 10) {
        printf("[AUDIO] waveout_write #%u: buf=0x%08X size=%u\n",
               m_audio_write_count, buf_addr, size);
    } else if (m_audio_write_count == 10) {
        printf("[AUDIO] waveout_write: subsequent calls suppressed\n");
    }

    m_last_waveout_bytes = -1;
    int bytes = audio_do_write(buf_addr, size);
    m_audio_write_count++;
    if (bytes > 0)
        m_audio_hw_chunk_bytes = (u32)bytes;
    if (bytes >= 0)
        m_last_waveout_bytes = bytes;
    if (bytes >= 0)
        g_cpu_regs[2] = (u32)bytes;
}

void Syscalls::impl_waveout_can_write() {
    if (m_nosound) { g_cpu_regs[2] = 65536; return; }
    g_cpu_regs[2] = (u32)audio_can_write_bytes();
}

void Syscalls::impl_pcm_can_write() {
    impl_waveout_can_write();
}

void Syscalls::impl_pcm_can_read() {
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_pcm_read() {
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_pcm_write() {
    u32 buf_addr = arg(0);
    u32 size = arg(1);
    if (m_nosound) { g_cpu_regs[2] = size; return; }
    int bytes = audio_do_write(buf_addr, size);
    if (bytes >= 0)
        g_cpu_regs[2] = (u32)bytes;
}

void Syscalls::impl_pcm_ioctl() {
    u32 cmd = arg(0);
    u32 arg_val = arg(1);

    if (m_nosound) {
        if (cmd == 5 /* PCM_GET_SPACE */) {
            if (arg_val) m_mem.write_u32(arg_val, 65536);
            g_cpu_regs[2] = 0;
        } else if (cmd == 4 /* PCM_GET_VOL */ || cmd == 7 /* PCM_GET_HP_VOL */) {
            g_cpu_regs[2] = m_pcm_volume;
        } else {
            g_cpu_regs[2] = 0;
        }
        return;
    }

    switch (cmd) {
#define PCM_SET_SAMPLE_RATE  0
#define PCM_SET_CHANNEL      1
#define PCM_SET_FORMAT       2
#define PCM_SET_VOL          3
#define PCM_GET_VOL          4
#define PCM_GET_SPACE        5
#define PCM_SET_HP_VOL       6
#define PCM_GET_HP_VOL       7
#define PCM_SET_PAUSE        8
#define PCM_SET_PLAY         9
#define PCM_RESET            10
#define PCM_SET_MUTE         13
    case PCM_GET_SPACE: {
        u32 free_bytes = (u32)audio_can_write_bytes();
        if (arg_val) m_mem.write_u32(arg_val, free_bytes);
        g_cpu_regs[2] = 0;
        break;
    }
    case PCM_SET_SAMPLE_RATE: {
        int rate = (int)arg_val;
        if (rate > 0 && rate != (int)m_audio_sample_rate) {
            m_audio_sample_rate = (u32)rate;
            audio_alloc_ring(m_audio_sample_rate, m_audio_channels);
            audio_open_device(rate, (int)m_audio_channels);
        }
        g_cpu_regs[2] = 0;
        break;
    }
    case PCM_SET_CHANNEL: {
        int ch = (int)arg_val;
        if (ch >= 1 && ch <= 2 && ch != (int)m_audio_channels) {
            m_audio_channels = (u32)ch;
            audio_alloc_ring(m_audio_sample_rate, m_audio_channels);
            audio_open_device((int)m_audio_sample_rate, ch);
        }
        g_cpu_regs[2] = 0;
        break;
    }
    case PCM_SET_FORMAT: {
        u32 bits = (arg_val == 8) ? 8u : 16u;
        if (bits != m_audio_bits)
            m_audio_bits = bits;
        g_cpu_regs[2] = 0;
        break;
    }
    case PCM_SET_PAUSE:
        m_audio_paused = true;
        if (m_audio_device > 0)
            SDL_PauseAudioDevice(m_audio_device, 1);
        g_cpu_regs[2] = 0;
        break;
    case PCM_SET_PLAY:
        m_audio_paused = false;
        if (m_audio_device > 0)
            SDL_PauseAudioDevice(m_audio_device, 0);
        g_cpu_regs[2] = 0;
        break;
    case PCM_RESET:
        audio_reset_ring();
        g_cpu_regs[2] = 0;
        break;
    case PCM_SET_MUTE:
        m_audio_muted = (arg_val != 0);
        g_cpu_regs[2] = 0;
        break;
    case PCM_SET_HP_VOL:
        audio_set_volume_level(arg_val);
        g_cpu_regs[2] = 0;
        break;
    case PCM_SET_VOL:
        m_pcm_volume = arg_val;
        g_cpu_regs[2] = 0;
        break;
    case PCM_GET_VOL:
    case PCM_GET_HP_VOL:
        g_cpu_regs[2] = m_pcm_volume;
        break;
    default:
        printf("[PCM] ioctl unknown cmd=%u arg=0x%08X\n", cmd, arg_val);
        g_cpu_regs[2] = 0;
        break;
    }
}

// === GOT 58-67: RTOS ===

void Syscalls::save_current_task() {
    int t = m_current_task;
    if (t < 0 || t >= m_task_count || t >= MAX_TASKS)
        return;
    // A KUSEG PC means the CPU already jumped off the rails — keep the last
    // valid resume address so a later switch can restart at task_entry.
    if ((g_cpu_pc & 0x80000000) == 0) {
        printf("[SCHEDULER] not saving KUSEG pc=0x%08X for task %d (keeping 0x%08X)\n",
               g_cpu_pc, t, m_tasks[t].pc);
        return;
    }
    memcpy(m_tasks[t].regs, g_cpu_regs, sizeof(g_cpu_regs));
    m_tasks[t].hi  = g_cpu_hi;
    m_tasks[t].lo  = g_cpu_lo;
    m_tasks[t].pc  = g_cpu_pc;
}

void Syscalls::switch_to_task(int task_idx) {
    if (task_idx < 0 || task_idx >= m_task_count) return;
    Task& t = m_tasks[task_idx];
    if ((t.pc & 0x80000000) == 0) {
        printf("[SCHEDULER] task %d invalid resume pc=0x%08X, resetting to entry 0x%08X\n",
               task_idx, t.pc, t.task_entry);
        t.pc = t.task_entry;
    }
    if ((t.pc & 0x80000000) == 0)
        return;
    memcpy(g_cpu_regs, t.regs, sizeof(g_cpu_regs));
    g_cpu_hi = t.hi;
    g_cpu_lo = t.lo;
    g_cpu_pc = t.pc;
    m_current_task   = task_idx;
    m_task_switched  = true;
}

int Syscalls::find_ready_task() {
    // Find the highest-priority (lowest task_prio value) ready task
    int best = -1;
    u8 best_prio = 255;
    for (int i = 0; i < m_task_count; i++) {
        if (m_tasks[i].active && !m_tasks[i].blocked && m_tasks[i].task_prio < best_prio) {
            best = i;
            best_prio = m_tasks[i].task_prio;
        }
    }
    return best;
}

void Syscalls::impl_OSTimeGet() {
    g_cpu_regs[2] = m_os_ticks;
}

void Syscalls::impl_OSSemCreate() {
    // OS_EVENT layout (OS_LOWEST_PRIO=254, OS_EVENT_PENDING_WATCH=1, OS_EVENT_NAME_SIZE=16):
    //   +0  OSEventType  u8   (3 = OS_EVENT_TYPE_SEM)
    //   +4  OSEventPtr   u32  (NULL for a live sem)
    //   +8  OSEventCnt   u16
    //   +10 OSEventGrp   u16
    //   +12 OSEventTbl   u16[16]
    //   +44 OSPendRA     u32
    //   +48 OSPendSP     u32
    //   +52 OSEventName  u8[16]
    //   total: 68 bytes
    u32 cnt = arg(0);
    u32 ecb = heap_alloc(68);
    m_mem.write_u8 (ecb + 0,  3);          // OSEventType = OS_EVENT_TYPE_SEM
    m_mem.write_u32(ecb + 4,  0);          // OSEventPtr  = NULL
    m_mem.write_u16(ecb + 8,  (u16)cnt);   // OSEventCnt
    m_mem.write_u16(ecb + 10, 0);          // OSEventGrp
    for (int i = 0; i < 16; i++) m_mem.write_u16(ecb + 12 + i * 2, 0);
    m_mem.write_u32(ecb + 44, 0);          // OSPendRA
    m_mem.write_u32(ecb + 48, 0);          // OSPendSP
    m_mem.write_u8 (ecb + 52, '?');        // OSEventName[0]
    m_mem.write_u8 (ecb + 53, 0);          // OSEventName[1]
    g_cpu_regs[2] = ecb;
    m_semaphores.push_back(ecb);
    printf("[OSSemCreate] ecb=0x%08X cnt=%u\n", ecb, cnt);
}

void Syscalls::impl_OSTaskCreate() {
    u32 entry = arg(0);
    u32 task_arg = arg(1);
    u32 stack_top = arg(2);
    u32 prio = arg(3);

    printf("[OSTaskCreate] entry=0x%08X arg=0x%08X stack=0x%08X prio=%u (task_count=%d)\n",
           entry, task_arg, stack_top, prio, m_task_count);

    // Real OSTaskCreate validates prio <= OS_LOWEST_PRIO (=254) and returns
    // OS_PRIO_INVALID = 38 otherwise.
    if (prio > 254) {
        printf("[OSTaskCreate] FAILED: prio %u > OS_LOWEST_PRIO\n", prio);
        g_cpu_regs[2] = 38; /* OS_PRIO_INVALID */
        return;
    }

    // Duplicate priorities are now allowed (original µC/OS-II check removed).
    // for (int i = 0; i < m_task_count; i++) {
    //     if (m_tasks[i].active && m_tasks[i].task_prio == (u8)prio) {
    //         printf("[OSTaskCreate] FAILED: duplicate priority %u for task %d -> OS_PRIO_EXIST\n", (u8)prio, i);
    //         g_cpu_regs[2] = 40; /* OS_PRIO_EXIST */
    //         return;
    //     }
    // }

    if (m_task_count < MAX_TASKS) {
        Task& t = m_tasks[m_task_count];
        memset(&t, 0, sizeof(t));
        t.active = true;
        t.blocked = false;
        t.task_arg = task_arg;
        t.task_prio = (u8)prio;
        t.task_entry = entry;
        t.wake_tick = 0;
        t.block_sem = 0;
        t.sem_err_ptr = 0;
        t.regs[4] = task_arg;
        t.regs[28] = g_cpu_regs[28];  // inherit $gp from the creator
        // Rubido (and other Vrix titles) pass a BSS symbol as ptos — a few
        // dozen bytes, not a real stack. The mixer then smashes $ra and jr's
        // to 0. Give in-image stacks a proper 16 KB heap frame.
        u32 sp = stack_top & ~0xF;
        if (sp >= 0x80A00000 && sp < 0x80C00000) {
            static constexpr u32 TASK_STACK_SIZE = 0x4000;
            u32 base = heap_alloc(TASK_STACK_SIZE);
            if (base) {
                printf("[OSTaskCreate] in-image stack 0x%08X too small; using 16KB @ 0x%08X\n",
                       sp, base);
                sp = (base + TASK_STACK_SIZE) & ~0xF;
            }
        }
        t.regs[29] = sp;
        t.regs[30] = sp;
        // Real µC/OS-II OSTaskStkInit sets $ra to OS_TaskReturn so a task that
        // falls off its entry deletes itself instead of jumping to 0 (KUSEG).
        t.regs[31] = TASK_RETURN_PC;
        t.pc = entry;

        int new_idx = m_task_count;
        m_task_count++;
        printf("[OSTaskCreate] Created task %d: entry=0x%08X prio=%u\n",
               new_idx, entry, (u8)prio);
        g_cpu_regs[2] = 0;

        if (m_scheduler_started) {
            u8 cur_prio = (m_current_task >= 0) ? m_tasks[m_current_task].task_prio : 255;
            if ((u8)prio < cur_prio) {
                save_current_task();
                switch_to_task(new_idx);
                printf("[OSTaskCreate] Preempted to task %d (prio %u < %u)\n",
                       new_idx, (u8)prio, cur_prio);
            }
        }
    } else {
        printf("[OSTaskCreate] FAILED: max tasks (%d) reached\n", MAX_TASKS);
        g_cpu_regs[2] = 0xFF;
    }
}


void Syscalls::sem_pend(u32 sem_ptr, u32 timeout, u32 err_ptr) {
    if (m_current_task >= 0 && m_current_task < m_task_count
        && m_tasks[m_current_task].task_prio >= 16 && sem_ptr) {
        audio_register_sem(sem_ptr);
    }
    u16 cnt = m_mem.read_u16(sem_ptr + 8);
    if (cnt > 0) {
        m_mem.write_u16(sem_ptr + 8, (u16)(cnt - 1));
        if (err_ptr) m_mem.write_u8(err_ptr, 0);
        g_cpu_regs[2] = 0;
        return;
    }
    if (m_current_task < 0 || m_current_task >= m_task_count) {
        if (err_ptr) m_mem.write_u8(err_ptr, 10);
        g_cpu_regs[2] = 10;
        return;
    }
    save_current_task();
    m_tasks[m_current_task].blocked = true;
    m_tasks[m_current_task].block_sem = sem_ptr;
    m_tasks[m_current_task].sem_err_ptr = err_ptr;
    m_tasks[m_current_task].wake_tick = (timeout > 0) ? (m_os_ticks + timeout) : 0;
    m_tasks[m_current_task].regs[2] = 0;
    int next = find_ready_task();
    if (next >= 0) {
        switch_to_task(next);
    } else {
        m_tasks[m_current_task].blocked = false;
        m_tasks[m_current_task].block_sem = 0;
        m_tasks[m_current_task].sem_err_ptr = 0;
        m_tasks[m_current_task].wake_tick = 0;
        if (err_ptr) m_mem.write_u8(err_ptr, 10);
        g_cpu_regs[2] = 10;
    }
}

void Syscalls::impl_OSSemPend() {
    sem_pend(arg(0), arg(1), arg(2));
}

void Syscalls::impl_OSSemPost() {
    u32 sem_ptr = arg(0);

    if (m_current_task >= 0 && m_current_task < m_task_count
        && m_tasks[m_current_task].task_prio >= 16
        && m_last_waveout_bytes == 0) {
        g_cpu_regs[2] = 0;
        return;
    }

    if (!sem_signal(sem_ptr)) {
        g_cpu_regs[2] = 51;  // OS_SEM_OVF
        return;
    }
    m_last_waveout_bytes = -1;
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_OSTimeDly() {
    u32 ticks = arg(0);

    if (ticks == 0) {
        g_cpu_regs[2] = 0;
        return;
    }

    // No current task — nothing to delay, just return.
    if (m_current_task < 0 || m_current_task >= m_task_count ||
        m_current_task >= MAX_TASKS) {
        g_cpu_regs[2] = 0;
        return;
    }

    save_current_task();
    m_tasks[m_current_task].blocked = true;
    m_tasks[m_current_task].wake_tick = m_os_ticks + ticks;
    m_tasks[m_current_task].regs[2] = 0;  // OS_NO_ERR returned when task resumes

    int next = find_ready_task();
    if (next >= 0) {
        switch_to_task(next);
        // g_cpu_regs now holds the next task's context — do NOT write to it here.
        // The delayed task's return value (0) is already stored in its saved regs above.
    } else {
        // No other task ready — spin in the idle loop until vsync advances ticks
        // and unblocks this task.  Do NOT switch to the dl_main sentinel PC.
        m_in_idle = true;
        m_current_task = -1;
        g_cpu_pc = m_idle_pc;
        m_task_switched = true;
    }
}

void Syscalls::impl_OSSemDel() {
    // OS_EVENT *OSSemDel(OS_EVENT *pevent, INT8U opt, INT8U *err)
    // opt: 0 = OS_DEL_NO_PEND, 1 = OS_DEL_ALWAYS
    u32 sem_ptr = arg(0);
    u32 opt     = arg(1);
    u32 err_ptr = arg(2);

    if (!sem_ptr) {
        if (err_ptr) m_mem.write_u8(err_ptr, 4); /* OS_ERR_PEVENT_NULL */
        g_cpu_regs[2] = sem_ptr;
        return;
    }
    if (m_mem.read_u8(sem_ptr + 0) != 3) {
        if (err_ptr) m_mem.write_u8(err_ptr, 9); /* OS_ERR_EVENT_TYPE */
        g_cpu_regs[2] = sem_ptr;
        return;
    }

    // Count tasks waiting on this sem.
    bool tasks_waiting = false;
    for (int i = 0; i < m_task_count; i++) {
        if (m_tasks[i].blocked && m_tasks[i].block_sem == sem_ptr) {
            tasks_waiting = true;
            break;
        }
    }

    if (opt == 0 /* OS_DEL_NO_PEND */ && tasks_waiting) {
        if (err_ptr) m_mem.write_u8(err_ptr, 8); /* OS_ERR_TASK_WAITING */
        g_cpu_regs[2] = sem_ptr;
        return;
    }

    // OS_DEL_ALWAYS (or NO_PEND with no waiters): wake every waiter with OS_ERR (event deleted).
    for (int i = 0; i < m_task_count; i++) {
        if (m_tasks[i].blocked && m_tasks[i].block_sem == sem_ptr) {
            if (m_tasks[i].sem_err_ptr)
                m_mem.write_u8(m_tasks[i].sem_err_ptr, 10); /* OS_TIMEOUT (best-effort) */
            m_tasks[i].sem_err_ptr = 0;
            m_tasks[i].block_sem = 0;
            m_tasks[i].wake_tick = 0;
            m_tasks[i].blocked = false;
        }
    }

    // Mark ECB as unused.
    m_mem.write_u8(sem_ptr + 0, 0); /* OS_EVENT_TYPE_UNUSED */
    m_mem.write_u16(sem_ptr + 8, 0);

    // Remove from our tracking list.
    for (auto it = m_semaphores.begin(); it != m_semaphores.end(); ++it) {
        if (*it == sem_ptr) { m_semaphores.erase(it); break; }
    }

    if (err_ptr) m_mem.write_u8(err_ptr, 0); /* OS_NO_ERR */
    g_cpu_regs[2] = 0;  /* NULL = success */
}

void Syscalls::set_idle_regs(const u32 regs[32]) {
    memcpy(m_idle_regs, regs, sizeof(m_idle_regs));
}

bool Syscalls::has_blocked_tasks() const {
    for (int i = 0; i < m_task_count; i++) {
        if (m_tasks[i].active && m_tasks[i].blocked)
            return true;
    }
    return false;
}

void Syscalls::impl_OSTaskDel() {
    // OSTaskDel(prio): 255 = OS_PRIO_SELF
    u32 prio = arg(0);
    int deleted_task = -1;

    if (prio == 255) {
        if (m_current_task < 0) {
            g_cpu_regs[2] = 2; /* OS_TASK_NOT_EXIST */
            return;
        }
        deleted_task = m_current_task;
    } else if (prio == 254) {
        // OS_TASK_IDLE_PRIO — real impl returns OS_TASK_DEL_IDLE
        g_cpu_regs[2] = 1; /* OS_TASK_DEL_IDLE */
        return;
    } else {
        for (int i = 0; i < m_task_count; i++) {
            if (m_tasks[i].active && m_tasks[i].task_prio == (u8)prio) {
                deleted_task = i;
                break;
            }
        }
        if (deleted_task < 0) {
            g_cpu_regs[2] = 2; /* OS_TASK_NOT_EXIST */
            return;
        }
    }

    if (deleted_task >= 0) {
        m_mem.write_u8(m_tasks[deleted_task].task_arg + 0x18C, 1);
        m_tasks[deleted_task].active = false;
        m_tasks[deleted_task].blocked = false;
        m_tasks[deleted_task].wake_tick = 0;
        m_tasks[deleted_task].block_sem = 0;
        m_tasks[deleted_task].sem_err_ptr = 0;
    }

    bool deleted_self = (deleted_task == m_current_task);
    g_cpu_regs[2] = 0; /* OS_NO_ERR */

    if (!deleted_self) {
        printf("[OSTaskDel] Deleted task %d (prio=%u) (caller keeps running)\n",
               deleted_task, prio);
        return;
    }

    m_current_task = -1;
    int next = find_ready_task();
    if (next >= 0) {
        switch_to_task(next);
        printf("[OSTaskDel] Deleted self (task %d), switched to task %d\n",
               deleted_task, next);
    } else {
        printf("[OSTaskDel] Deleted self (task %d), returning to caller (no ready task)\n", deleted_task);
        m_in_idle = true;
    }
}

void Syscalls::impl_GetTickCount() {
    // Native firmware returns ~microseconds: OSTimeGet()*10000 plus a small
    // unsynchronized TCU remnant (see dingoo_coding). xrick (Rick-Dangerous)
    // divides by 500/1000 treating the value as µs; returning host milliseconds
    // made delay(50) spin for 25 seconds on a blank framebuffer.
    g_cpu_regs[2] = (u32)((u64)SDL_GetTicks() * 1000ull);
}

void Syscalls::impl__sys_judge_event() {
    // The real _sys_judge_event (VA 0x801364B0) is a fast, non-blocking routine.
    // It reads the touch-screen registers (0x8057F16A/C/170) — always inactive on
    // the Dingoo A320 (no touchscreen) — reads the global key-state at 0x80242B40,
    // translates via a runtime table, and returns immediately.

    // EVENT_QUEUE_ADDR (0x80BFECD8) is pre-populated by the loader with a
    // hardware-ready sentinel (bit 31 set) that the game reads directly during
    // audio init.  If _sys_judge_event ever sees it, clear it silently — games
    // treat any bit-31 value here as an OS exit/state signal, not a normal event.
    // Checking bit 31 rather than one empirically-observed value (0x8BFC4D89)
    // covers all Dingoo apps regardless of the exact sentinel written.
    u32 event_val = m_mem.read_u32(EVENT_QUEUE_ADDR);
    if (event_val & 0x80000000u) {
        m_mem.write_u32(EVENT_QUEUE_ADDR, 0);
        event_val = 0;
    } else if (event_val) {
        m_mem.write_u32(EVENT_QUEUE_ADDR, 0);
        g_cpu_regs[2] = event_val;
        return;
    }

    // Dequeue edge events (key-down / key-up) first
    if (m_display.has_input_event()) {
        event_val = m_display.pop_input_event();
        g_cpu_regs[2] = event_val;
        return;
    }

    // No edge events — check for held keys via the shared key-state memory
    // (0x80242B40 = KERN_KEY_SCAN_VAL, written every frame by the main loop).
    // The real function reads this location to detect held keys.
    u32 keys = m_mem.read_u32(0x80242B40 & 0x1FFFFFFF);
    if (keys == 0) keys = m_display.get_dingoo_keys();  // fallback
    if (keys) {
        // Find lowest-set-bit key and report it as held (type 3)
        u32 single = keys & (~keys + 1);  // isolate lowest bit
        u32 code = bitmask_to_keycode(single);
        if (code) {
            event_val = (3u << 8) | code;  // type 3 = held
            g_cpu_regs[2] = event_val;
            return;
        }
    }

    g_cpu_regs[2] = 0;
}

// === GOT 68-71: unicode / locale ===

void Syscalls::impl_fsys_fopenW() {
    u32 path_addr = arg(0);
    u32 mode_addr = arg(1);

    if (path_addr == 0 || mode_addr == 0) { g_cpu_regs[2] = 0; return; }

    // Read UTF-16LE path
    std::string path;
    for (u32 i = 0; i < 512; i++) {
        u16 c = m_mem.read_u16(path_addr + i * 2);
        if (c == 0) break;
        if (c < 128) path += (char)c;
        else path += '?';
    }

    if (path.empty() || path.find_first_not_of(' ') == std::string::npos) {
        g_cpu_regs[2] = 0;
        return;
    }

    // Read mode (UTF-16LE too)
    std::string mode;
    for (u32 i = 0; i < 16; i++) {
        u16 c = m_mem.read_u16(mode_addr + i * 2);
        if (c == 0) break;
        if (c < 128) mode += (char)c;
        else mode += '?';
    }

    // If path matches the app binary name (with or without .app extension),
    // the game is opening its own binary to read embedded data (resource offsets, etc.).
    // Extract the basename from m_app_path for comparison.
    bool is_self = false;
    if (!m_app_path.empty()) {
        std::string self_name = m_app_path;
        size_t slash = self_name.find_last_of("/\\");
        if (slash != std::string::npos) self_name = self_name.substr(slash + 1);
        // Try exact match, match without .app, match lowercased
        if (path == self_name) is_self = true;
        size_t dot = self_name.find_last_of('.');
        if (dot != std::string::npos && path == self_name.substr(0, dot)) is_self = true;
        std::string path_lower = path;
        std::transform(path_lower.begin(), path_lower.end(), path_lower.begin(), ::tolower);
        std::string self_lower = self_name;
        std::transform(self_lower.begin(), self_lower.end(), self_lower.begin(), ::tolower);
        if (path_lower == self_lower) is_self = true;
    }
    if (is_self) {
        int idx = alloc_file_handle();
        if (idx < 0) { g_cpu_regs[2] = 0; return; }
        FILE* f = fopen(m_app_path.c_str(), "rb");
        if (f) {
            m_files[idx].is_host = true;
            m_files[idx].host_file = f;
            printf("[fopenW] '%s' mode='%s' -> handle %d (app binary)\n",
                   path.c_str(), mode.c_str(), idx + 1);
            g_cpu_regs[2] = (u32)idx + 1;
            return;
        }
        m_files[idx].in_use = false;
    }

    // If path is garbage, try game init files in order
    std::string search_path = path;
    /*if (search_path.empty() || search_path.find_first_not_of(' ') == std::string::npos ||
        search_path.find('?') != std::string::npos) {
        // Game constructs paths using game name from BSS, which gets overwritten.
        // Fall back to expected init files in priority order.
        static const char* const init_files[] = {
            ".\\ui\\state.sdt",
            ".\\audio\\WARPlayer.exe",
        };
        for (auto f : init_files) {
            if (m_archive && m_archive->find(f)) {
                search_path = f;
                printf("[fopenW] PATH FALLBACK -> '%s'\n", search_path.c_str());
                break;
            }
        }
    }*/

    if (m_archive && !is_spk_index_bin(search_path)) {
        const ArchiveEntry* entry = m_archive->find(search_path);
        if (!entry && search_path.size() > 2 && search_path[0] == '.' &&
            (search_path[1] == '\\' || search_path[1] == '/'))
            entry = m_archive->find(search_path.substr(2));
        if (!entry && search_path.size() > 4 &&
            (search_path.substr(0, 4) == "res\\" || search_path.substr(0, 4) == "res/"))
            entry = m_archive->find(search_path.substr(4));
        if (!entry) {
            size_t slash = search_path.find_last_of("/\\");
            if (slash != std::string::npos)
                entry = m_archive->find(search_path.substr(slash + 1));
        }
        if (entry) {
            int idx = alloc_file_handle();
            if (idx < 0) { g_cpu_regs[2] = 0; return; }
            m_files[idx].is_archive = true;
            m_files[idx].archive = m_archive;
            m_files[idx].archive_entry = entry;
            m_files[idx].offset = 0;
            printf("[fopenW] '%s' mode='%s' -> handle %d (archive, %u bytes)\n",
                   search_path.c_str(), mode.c_str(), idx + 1, (u32)entry->size);
            g_cpu_regs[2] = (u32)idx + 1;
            return;
        }
    }

    // Try host filesystem for save files
    int idx = alloc_file_handle();
    if (idx < 0) { g_cpu_regs[2] = 0; return; }

    if (mode.find('w') != std::string::npos || mode.find('+') != std::string::npos) {
        std::string host_path = "save/" + path;
        FILE* f = fopen(host_path.c_str(), mode.c_str());
        if (f) {
            m_files[idx].is_host = true;
            m_files[idx].host_file = f;
            printf("[fopenW] '%s' mode='%s' -> handle %d (host write)\n",
                   path.c_str(), mode.c_str(), idx + 1);
            g_cpu_regs[2] = (u32)idx + 1;
            return;
        }
    }

    // Try reading from host save/ too
    if (mode.find('r') != std::string::npos) {
        std::string host_path = "save/" + path;
        FILE* f = fopen(host_path.c_str(), "rb");
        if (f) {
            m_files[idx].is_host = true;
            m_files[idx].host_file = f;
            printf("[fopenW] '%s' mode='%s' -> handle %d (host read)\n",
                   path.c_str(), mode.c_str(), idx + 1);
            g_cpu_regs[2] = (u32)idx + 1;
            return;
        }
    }

    printf("[fopenW] '%s' mode='%s' -> NOT FOUND\n", search_path.c_str(), mode.c_str());
    m_files[idx].in_use = false;
    g_cpu_regs[2] = 0;
}

void Syscalls::impl___to_unicode_le() {
    u32 src = arg(0);
    if (!src) { g_cpu_regs[2] = 0; return; }

    // Read ASCII source string
    char buf[1024];
    u32 len = 0;
    for (u32 i = 0; i < sizeof(buf) - 1; i++) {
        u8 c = m_mem.read_u8(src + i);
        buf[i] = (char)c;
        if (c == 0) { len = i; break; }
    }
    if (len == 0) { g_cpu_regs[2] = 0; return; }

    // Allocate wide buffer in guest heap
    u32 dst = heap_alloc((len + 1) * 2);
    if (!dst) { g_cpu_regs[2] = 0; return; }
    u32 dst_phys = dst & 0x1FFFFFFF;

    // Write UTF-16LE (naive ASCII)
    for (u32 i = 0; i < len; i++) {
        u16 wc = (u8)buf[i] < 128 ? (u16)(u8)buf[i] : (u16)'?';
        m_mem.write_u16(dst_phys + i * 2, wc);
    }
    m_mem.write_u16(dst_phys + len * 2, 0); // null terminator

    printf("[UNICODE] __to_unicode_le(\"%s\") -> 0x%08X\n", buf, dst);
    g_cpu_regs[2] = dst;
}

void Syscalls::impl___to_locale_ansi() {
    u32 src = arg(0);
    u32 dst_arg = arg(1);
    u32 max_len = arg(2);
    printf("[UNICODE] __to_locale_ansi(src=0x%08X dst=0x%08X max=%u)\n", src, dst_arg, max_len);
    if (!src) { g_cpu_regs[2] = 0; return; }

    // Read UTF-16LE source
    u16 wide[512];
    u32 len = 0;
    for (u32 i = 0; i < 512; i++) {
        u16 c = m_mem.read_u16(src + i * 2);
        wide[i] = c;
        if (c == 0) { len = i; break; }
    }
    if (len == 0) { printf("[UNICODE] __to_locale_ansi: src has no content (all zero?)\n"); g_cpu_regs[2] = 0; return; }

    // Allocate ASCII buffer in guest heap
    u32 dst = heap_alloc(len + 1);
    if (!dst) { g_cpu_regs[2] = 0; return; }
    u32 dst_phys = dst & 0x1FFFFFFF;

    // Write low byte of each wchar_t (naive ASCII)
    for (u32 i = 0; i < len; i++) {
        u8 c = (wide[i] < 128) ? (u8)wide[i] : '?';
        m_mem.write_u8(dst_phys + i, c);
    }
    m_mem.write_u8(dst_phys + len, 0); // null terminator

    printf("[UNICODE] __to_locale_ansi(0x%08X, %u chars) -> 0x%08X\n", src, len, dst);
    g_cpu_regs[2] = dst;
}

void Syscalls::impl_get_current_language() {
    int lang = 0; // Chinese (0=English)
    printf("[STUB] get_current_language -> %d (%s)\n", lang, lang == 0 ? "English" : "Chinese");
    g_cpu_regs[2] = lang; // Chinese (0=English)
}

// === dl_res resource API (SPK archive by guest path) ===

int Syscalls::alloc_dl_res_handle() {
    for (int i = 0; i < MAX_DL_RES; i++) {
        if (!m_dl_res[i].in_use)
            return i;
    }
    return -1;
}

void Syscalls::free_dl_res_handle(int idx) {
    if (idx < 0 || idx >= MAX_DL_RES) return;
    m_dl_res[idx].in_use = false;
    m_dl_res[idx].guest_addr = 0;
    m_dl_res[idx].size = 0;
    m_dl_res[idx].offset = 0;
    m_dl_res[idx].host_data = nullptr;
}

const ArchiveEntry* Syscalls::find_dl_file(const std::string& name) const {
    if (!m_archive || name.empty())
        return nullptr;
    if (const ArchiveEntry* e = m_archive->find(name))
        return e;
    std::string stripped = name;
    if (stripped.size() >= 3 && stripped[1] == ':' &&
        (stripped[2] == '\\' || stripped[2] == '/'))
        stripped = stripped.substr(3);
    if (const ArchiveEntry* e = m_archive->find(stripped))
        return e;
    size_t slash = stripped.find_last_of("/\\");
    if (slash != std::string::npos)
        return m_archive->find(stripped.substr(slash + 1));
    return nullptr;
}

const ArchiveEntry* Syscalls::resolve_dl_res_entry(u32 key_or_path) {
    if (!m_archive || m_archive->count() == 0)
        return nullptr;

    if (key_or_path < 0x80000000)
        return nullptr;

    std::string path = read_guest_path(key_or_path);
    if (path.empty())
        return nullptr;

    if (const ArchiveEntry* e = m_archive->find(path))
        return e;
    size_t slash = path.find_last_of("/\\");
    if (slash != std::string::npos)
        return m_archive->find(path.substr(slash + 1));
    return nullptr;
}

void Syscalls::impl_get_dl_handle() {
    g_cpu_regs[2] = 1;
}

void Syscalls::impl_dl_res_open() {
    const ArchiveEntry* entry = resolve_dl_res_entry(arg(0));
    if (!entry)
        entry = resolve_dl_res_entry(arg(2));
    if (!entry) {
        g_cpu_regs[2] = 0;
        return;
    }

    int idx = alloc_dl_res_handle();
    if (idx < 0) { g_cpu_regs[2] = 0; return; }

    // Keep the payload on the host. Mapping every open into guest RAM made
    // Rubido's 10 MB music.snd bump the heap into the stack and smash $ra.
    m_dl_res[idx].in_use = true;
    m_dl_res[idx].guest_addr = 0;
    m_dl_res[idx].size = entry->size;
    m_dl_res[idx].offset = 0;
    m_dl_res[idx].host_data = m_archive->get_data(*entry);

    printf("[dl_res] open '%s' -> handle %d (%u bytes, host-backed)\n",
           entry->name.c_str(), idx + 1, entry->size);
    g_cpu_regs[2] = (u32)(idx + 1);
}

void Syscalls::impl_dl_res_get_size() {
    int idx = (int)arg(0) - 1;
    if (idx < 0 || idx >= MAX_DL_RES || !m_dl_res[idx].in_use)
        g_cpu_regs[2] = 0;
    else
        g_cpu_regs[2] = m_dl_res[idx].size;
}

void Syscalls::impl_dl_res_get_data() {
    int idx = (int)arg(0) - 1;
    if (idx < 0 || idx >= MAX_DL_RES || !m_dl_res[idx].in_use) {
        g_cpu_regs[2] = 0;
        return;
    }
    const u8* host = m_dl_res[idx].host_data;
    u32 dest = arg(1);
    u32 n = arg(2);
    u32 count = arg(3);
    u32 remaining = (m_dl_res[idx].offset < m_dl_res[idx].size)
        ? (m_dl_res[idx].size - m_dl_res[idx].offset) : 0;

    // Dingoo: dl_res_get_data(h, buf, size, nmemb) — total bytes = size*nmemb,
    // matching fread. Overlord reads a 36-byte DLX2 header (36, 1), then the
    // 12-byte index (12, count), then each payload (1, size).
    u32 nbytes = n;
    if (count > 1) {
        u64 total = (u64)n * count;
        if (total > 0 && total <= remaining)
            nbytes = (u32)total;
    }

    // Copy into a guest buffer when dest is KSEG0 RAM and the length fits.
    // Full-size dumps (PoPo) always copy. Partial reads (Overlord DLX2 header
    // and records) skip dest in the RAWD/code window so leftover $a1 from
    // brick-style callers cannot smash the guest image.
    u32 phys = dest & 0x1FFFFFFF;
    bool dest_ok = dest >= 0x80000000 && dest < 0x82000000;
    bool in_rawd = phys >= 0x00A00000 && phys < 0x00BF0000;
    if (dest_ok && nbytes > 0 && nbytes <= remaining && !in_rawd && host) {
        m_mem.write_block(dest, host + m_dl_res[idx].offset, nbytes);
        m_dl_res[idx].offset += nbytes;
        g_cpu_regs[2] = dest;
        return;
    }

    // Caller wants a pointer into the resource. Map once, lazily.
    if (!m_dl_res[idx].guest_addr && host && m_dl_res[idx].size) {
        u32 guest = heap_alloc(m_dl_res[idx].size);
        if (guest) {
            m_mem.write_block(guest, host, m_dl_res[idx].size);
            m_dl_res[idx].guest_addr = guest;
        }
    }
    g_cpu_regs[2] = m_dl_res[idx].guest_addr;
}

void Syscalls::impl_dl_res_close() {
    int idx = (int)arg(0) - 1;
    if (idx >= 0 && idx < MAX_DL_RES && m_dl_res[idx].in_use) {
        heap_free(m_dl_res[idx].guest_addr);
        free_dl_res_handle(idx);
    }
    g_cpu_regs[2] = 0;
}

// === GOT 77-86: LCD wrappers, input wrappers, µC/GUI helpers ===

void Syscalls::impl_lcd_set_frame() {
    // Non-underscore wrapper: same as _lcd_set_frame
    impl__lcd_set_frame();
}

void Syscalls::impl_lcd_get_frame() {
    // Non-underscore wrapper: same as _lcd_get_frame
    impl__lcd_get_frame();
}

void Syscalls::impl_lcd_get_bpp() {
    if (m_lcd_bpp == 1) g_cpu_regs[2] = 8;
    else if (m_lcd_bpp >= 4) g_cpu_regs[2] = 32;
    else g_cpu_regs[2] = 16;
}

void Syscalls::impl_LCD_GetXSize() {
    g_cpu_regs[2] = Display::WIDTH;
}

void Syscalls::impl_LCD_GetYSize() {
    g_cpu_regs[2] = Display::HEIGHT;
}

void Syscalls::impl_LCD_Color2Index() {
    // Real firmware signature: int LCD_Color2Index(uint16_t color, uint32_t *index_out)
    // a0 = 16-bit RGB565 color (zero-extended), a1 = output pointer
    // For direct-color displays the palette index equals the color value.
    u32 color = arg(0) & 0xFFFF;
    u32 out_ptr = arg(1);
    if (out_ptr)
        m_mem.write_u32(out_ptr, color);
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_kbd_get_key() {
    // kbd_get_key() (no underscore) returns the current hardware key bitmask.
    // Overlord's hold-check at 0x80A117FC ANDs this with KeyLeft/KeyA/etc.
    // _kbd_get_key is the separate event-FIFO API (type<<8|code).
    g_cpu_regs[2] = m_display.get_hw_keys();
}

void Syscalls::impl_kbd_get_status() {
    // Non-underscore wrapper: same as _kbd_get_status
    impl__kbd_get_status();
}

void Syscalls::impl_sys_judge_event() {
    // Non-underscore wrapper: same as _sys_judge_event
    impl__sys_judge_event();
}

u32 Syscalls::dingoo_code_to_gui_key(u32 code) {
    // Dingoo SDK keycodes 1–12 → µC/GUI GUI_KEY_*. Overlord's menu switch
    // (0x80A00830) and hold-table (0x80A21C70) compare against these values.
    switch (code) {
    case 0x01: return GUI_KEY_ENTER;   // A
    case 0x02: return GUI_KEY_ESCAPE;  // B
    case 0x03: return GUI_KEY_ENTER;   // X
    case 0x04: return GUI_KEY_SPACE;   // Y
    case 0x07: return GUI_KEY_ENTER;   // START
    case 0x08: return GUI_KEY_ESCAPE;  // SELECT
    case 0x09: return GUI_KEY_UP;
    case 0x0A: return GUI_KEY_DOWN;
    case 0x0B: return GUI_KEY_LEFT;
    case 0x0C: return GUI_KEY_RIGHT;
    default:   return 0;
    }
}

void Syscalls::impl_open_gui_key_msg() {
    // Real firmware starts a task that polls the keypad and posts WM_KEY
    // through GUI_StoreKeyMsg.  We fold that into GUI_Exec instead.
    m_gui_key_msg_open = true;
    printf("[µC/GUI] open_gui_key_msg\n");
    g_cpu_regs[2] = 0;
}

bool Syscalls::gui_dispatch_pending_key() {
    if (!m_gui_key_msg_open || !m_wm_callback)
        return false;
    if (!m_display.has_key_event())
        return false;
    u32 ev = m_display.pop_key_event();
    u32 type = ev >> 8;
    u32 gui_key = dingoo_code_to_gui_key(ev & 0xFFu);
    if (!gui_key)
        return false;
    u32 info = wm_scratch(m_wm_key_info_buf, 8);
    if (!info)
        return false;
    // WM_KEY_INFO { int Key; int PressedCnt; } — PressedCnt 1 = down, 0 = up.
    u32 pressed = (type == Display::EVT_KEY_DOWN) ? 1u : 0u;
    m_mem.write_u32(info + 0, gui_key);
    m_mem.write_u32(info + 4, pressed);
    printf("[µC/GUI] WM_KEY key=%u pressed=%u\n", gui_key, pressed);
    wm_dispatch(WM_MSG_KEY, info);
    return true;
}

// === Non‑standard GOT app functions (Yi‑Chi, Overlord‑Fighter) ===

// cmGetSysVersion / cmGetSysModel are called two ways by different apps:
//   ptr = cmGetSysXxx();          — no argument, use the returned string
//   cmGetSysXxx(wchar_t* buf);    — fill the caller's buffer
// The buffer form is wide (UTF-16LE): Overlord-Fighter passes the result straight
// to __to_locale_ansi(wchar_t*) and compares it against "GM760" / "A320", exiting
// AppMain when neither matches.  Serve both forms: fill a0 when it is a writable
// guest pointer, and always return a static ASCII copy for the no-argument callers.
void Syscalls::cm_write_sys_string(const char* text, u32 ascii_addr) {
    u32 len = (u32)strlen(text);

    for (u32 i = 0; i < len; i++)
        m_mem.write_u8(ascii_addr + i, (u8)text[i]);
    m_mem.write_u8(ascii_addr + len, 0);

    u32 buf = arg(0);
    if (buf >= 0x80000000 && (buf & 0x1FFFFFFF) + (len + 1) * 2 <= m_mem.size()) {
        for (u32 i = 0; i < len; i++)
            m_mem.write_u16(buf + i * 2, (u16)(u8)text[i]);
        m_mem.write_u16(buf + len * 2, 0);
        g_cpu_regs[2] = buf;
        return;
    }

    g_cpu_regs[2] = ascii_addr;
}

void Syscalls::impl_cmGetSysVersion() {
    cm_write_sys_string("V1.0", 0x80C0FF00);
}

void Syscalls::impl_cmGetSysModel() {
    cm_write_sys_string("A320", 0x80C0FF20);
}

void Syscalls::impl_mdelay() {
    // On real hardware this spins the CPU for arg(0) ms.
    // In the emulator, wall-clock spinning would stall the SDL event loop and
    // audio callback thread, and the emulator runs far from real-time anyway.
    // Simply returning keeps guest execution flowing at emulator speed.
    (void)arg(0);
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_fsys_clearerr() {
    u32 handle = arg(0);
    printf("[FSYS] clearerr(%u)\n", handle);
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_OSQCreate() {
    u32 size = arg(0);
    u32 ecb = heap_alloc(56);
    printf("[OS] OSQCreate(size=%u) -> ecb=0x%08X\n", size, ecb);
    g_cpu_regs[2] = ecb ? ecb : (u32)-1;
}

void Syscalls::impl_OSFlagPost() {
    u32 grp = arg(0);
    u32 flags = arg(1);
    u32 opt = arg(2);
    u32 err_ptr = arg(3);
    printf("[OS] OSFlagPost(grp=0x%08X flags=0x%08X opt=%u)\n", grp, flags, opt);
    if (err_ptr) m_mem.write_u8(err_ptr, 0);
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_SysEnableShutDownPower() {
    printf("[STUB] SysEnableShutDownPower\n");
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_SysDisableCloseBkLight() {
    printf("[STUB] SysDisableCloseBkLight\n");
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_GUI_Lock() {
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_GUI_Unlock() {
    g_cpu_regs[2] = 0;
}

// --- Minimal µC/GUI window manager ---

u32 Syscalls::wm_scratch(u32& slot, u32 size) {
    if (!slot)
        slot = heap_alloc(size);
    return slot;
}

// Build a WM_MESSAGE { int MsgId; U16 hWin; U16 hWinSrc; U32 Data; } and run the
// window callback as a guest call.  The callback returns through the stub at
// 0x80BFFF00, which resumes whatever invoked us.
void Syscalls::wm_dispatch(int msg_id, u32 data) {
    if (!m_wm_callback)
        return;
    u32 msg = wm_scratch(m_wm_msg_buf, 12);
    if (!msg)
        return;
    m_mem.write_u32(msg + 0, (u32)msg_id);
    m_mem.write_u16(msg + 4, (u16)WM_MAIN_HWIN);
    m_mem.write_u16(msg + 6, (u16)WM_MAIN_HWIN);
    m_mem.write_u32(msg + 8, data);
    call_guest_function(m_wm_callback, msg);
    m_task_switched = true;
}

bool Syscalls::gui_run_due_timer() {
    u32 now = SDL_GetTicks();
    for (auto& t : m_gui_timers) {
        if (!t.active || !t.callback || now < t.next_due)
            continue;
        // Re-arm defensively; the callback normally calls SetPeriod + Restart itself.
        t.next_due = now + (t.period_ms ? t.period_ms : 1);
        u32 tm = wm_scratch(m_gui_timer_msg_buf, 8);
        if (tm) {
            m_mem.write_u32(tm + 0, now);
            m_mem.write_u32(tm + 4, t.context);
        }
        call_guest_function(t.callback, tm);
        m_task_switched = true;
        return true;
    }
    return false;
}

void Syscalls::impl_GUI_Exec() {
    g_cpu_regs[2] = 0;
    if (m_wm_paint_pending) {
        m_wm_paint_pending = false;
        wm_dispatch(WM_MSG_CREATE, 0);
        return;
    }
    if (gui_dispatch_pending_key())
        return;
    gui_run_due_timer();
}

void Syscalls::impl_WM__SendMessage() {
    // WM__SendMessage(WM_HWIN hWin, WM_MESSAGE* pMsg)
    u32 pmsg = arg(1);
    if (!m_wm_callback || !pmsg) {
        g_cpu_regs[2] = 0;
        return;
    }
    m_mem.write_u16(pmsg + 4, (u16)arg(0));
    m_mem.write_u16(pmsg + 6, (u16)arg(0));
    g_cpu_regs[2] = 0;
    call_guest_function(m_wm_callback, pmsg);
    m_task_switched = true;
}

void Syscalls::impl_GUI_TIMER_Create() {
    // GUI_TIMER_Create(cb, Time, Context, Flags) -> handle
    u32 cb = arg(0);
    u32 period = arg(1);
    u32 context = arg(2);

    int idx = -1;
    for (size_t i = 0; i < m_gui_timers.size(); i++) {
        if (!m_gui_timers[i].active) { idx = (int)i; break; }
    }
    if (idx < 0) {
        idx = (int)m_gui_timers.size();
        m_gui_timers.push_back({});
    }
    GuiTimer& t = m_gui_timers[(size_t)idx];
    t.active = true;
    t.callback = cb;
    t.context = context;
    t.period_ms = period;
    t.next_due = SDL_GetTicks() + period;
    printf("[µC/GUI] GUI_TIMER_Create(cb=0x%08X period=%u ctx=0x%08X) -> %d\n",
           cb, period, context, idx + 1);
    g_cpu_regs[2] = (u32)(idx + 1);
}

void Syscalls::impl_GUI_TIMER_SetPeriod() {
    u32 handle = arg(0);
    u32 period = arg(1);
    if (handle >= 1 && handle <= m_gui_timers.size())
        m_gui_timers[handle - 1].period_ms = period;
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_GUI_TIMER_Restart() {
    u32 handle = arg(0);
    if (handle >= 1 && handle <= m_gui_timers.size()) {
        GuiTimer& t = m_gui_timers[handle - 1];
        t.next_due = SDL_GetTicks() + t.period_ms;
    }
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_GUI_TIMER_Delete() {
    u32 handle = arg(0);
    if (handle >= 1 && handle <= m_gui_timers.size())
        m_gui_timers[handle - 1].active = false;
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_WM_DefaultProc() {
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_WM_SelectWindow() {
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_WM_CreateWindow() {
    // WM_CreateWindow(x0, y0, width, height, Style, cb, NumExtraBytes) -> WM_HWIN
    u32 x0 = arg(0), y0 = arg(1), w = arg(2), h = arg(3);
    u32 style = arg(4);
    u32 cb = arg(5);
    printf("[µC/GUI] WM_CreateWindow(%u,%u %ux%u style=0x%X cb=0x%08X) -> hwin=%u\n",
           x0, y0, w, h, style, cb, WM_MAIN_HWIN);
    m_wm_callback = cb;
    m_wm_paint_pending = true;
    g_cpu_regs[2] = WM_MAIN_HWIN;
}

void Syscalls::impl_WM_DeleteWindow() {
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_WM_SetFocus() {
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_spin_lock_irqsave() {
    g_cpu_regs[2] = 0; // return previous SR (dummy)
}

void Syscalls::impl_spin_unlock_irqrestore() {
    // arg(0) = flags (ignored)
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_jz_pm_pllconvert() {
    printf("[STUB] jz_pm_pllconvert\n");
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_dl_load() {
    // void *dl_load(const char *filename) — map a named module from the
    // ERPT/SPK archive (or a sidecar next to the .app) into guest RAM.
    u32 name_ptr = arg(0);
    if (!name_ptr) {
        g_cpu_regs[2] = 0;
        return;
    }
    std::string name = guest_string(name_ptr);
    const ArchiveEntry* entry = find_dl_file(name);

    std::vector<u8> blob;
    if (entry) {
        blob.resize(entry->size);
        memcpy(blob.data(), m_archive->get_data(*entry), entry->size);
    } else if (!m_app_path.empty()) {
        std::string dir = m_app_path;
        size_t slash = dir.find_last_of("/\\");
        dir = (slash != std::string::npos) ? dir.substr(0, slash + 1) : std::string();
        std::string base = name;
        size_t nslash = base.find_last_of("/\\");
        if (nslash != std::string::npos)
            base = base.substr(nslash + 1);
        FILE* f = fopen((dir + base).c_str(), "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            if (sz > 0) {
                blob.resize((size_t)sz);
                fseek(f, 0, SEEK_SET);
                if (fread(blob.data(), 1, blob.size(), f) != blob.size())
                    blob.clear();
            }
            fclose(f);
        }
    }

    if (blob.empty()) {
        printf("[DL] dl_load('%s') -> not found\n", name.c_str());
        g_cpu_regs[2] = 0;
        return;
    }

    int slot = -1;
    for (int i = 0; i < MAX_DL_MODULES; i++) {
        if (!m_dl_modules[i].in_use) { slot = i; break; }
    }
    if (slot < 0) {
        printf("[DL] dl_load('%s') -> no module slots\n", name.c_str());
        g_cpu_regs[2] = 0;
        return;
    }

    u32 guest = heap_alloc((u32)blob.size());
    if (!guest) {
        printf("[DL] dl_load('%s') -> OOM (%zu bytes)\n", name.c_str(), blob.size());
        g_cpu_regs[2] = 0;
        return;
    }
    m_mem.write_block(guest, blob.data(), (u32)blob.size());

    m_dl_modules[slot].in_use = true;
    m_dl_modules[slot].guest_addr = guest;
    m_dl_modules[slot].size = (u32)blob.size();
    m_dl_modules[slot].name = name;

    char magic[5] = {};
    memcpy(magic, blob.data(), std::min(blob.size(), (size_t)4));
    printf("[DL] dl_load('%s') -> handle %d @ 0x%08X (%zu bytes, magic='%s')\n",
           name.c_str(), slot + 1, guest, blob.size(), magic);
    g_cpu_regs[2] = (u32)(slot + 1);
}

void Syscalls::impl_dl_free() {
    int idx = (int)arg(0) - 1;
    if (idx < 0 || idx >= MAX_DL_MODULES || !m_dl_modules[idx].in_use) {
        g_cpu_regs[2] = (u32)-1;
        return;
    }
    printf("[DL] dl_free(%d) '%s'\n", idx + 1, m_dl_modules[idx].name.c_str());
    heap_free(m_dl_modules[idx].guest_addr);
    m_dl_modules[idx].in_use = false;
    m_dl_modules[idx].guest_addr = 0;
    m_dl_modules[idx].size = 0;
    m_dl_modules[idx].name.clear();
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_dl_get_proc() {
    int idx = (int)arg(0) - 1;
    u32 name_ptr = arg(1);
    if (idx < 0 || idx >= MAX_DL_MODULES || !m_dl_modules[idx].in_use) {
        g_cpu_regs[2] = 0;
        return;
    }
    std::string name = name_ptr ? guest_string(name_ptr) : "";
    // Mapped blobs (DLX2 resource packs, raw CCDL) have no reloc'd export
    // table. Return the load address so callers can inspect the image.
    printf("[DL] dl_get_proc(%d, '%s') -> 0x%08X\n",
           idx + 1, name.c_str(), m_dl_modules[idx].guest_addr);
    g_cpu_regs[2] = m_dl_modules[idx].guest_addr;
}

void Syscalls::impl_U8TOU16() {
    // u16 U8TOU16(u8 *p) — little-endian 16-bit load (Dingoo SDK).
    u32 src = arg(0);
    g_cpu_regs[2] = src ? (u32)m_mem.read_u16(src) : 0;
}

void Syscalls::impl_U8TOU32() {
    // u32 U8TOU32(u8 *p) — little-endian 32-bit load (Dingoo SDK).
    u32 src = arg(0);
    g_cpu_regs[2] = src ? m_mem.read_u32(src) : 0;
}

// === VSYNC simulation ===

void Syscalls::register_main_context(u32 pc, u32 a0, u8 prio) {
    if (m_task_count >= MAX_TASKS) return;
    int idx = m_task_count++;
    Task& t = m_tasks[idx];
    memset(&t, 0, sizeof(t));
    t.active    = true;
    t.blocked   = false;
    t.task_prio = prio;
    t.task_arg  = a0;
    t.task_entry = pc;
    t.pc        = pc;
    // regs[] populated by save_current_task() on first preemption/block
    m_current_task      = idx;
    m_scheduler_started = true;
    printf("[SCHEDULER] Registered AppMain as task %d (prio %u) at 0x%08X\n", idx, prio, pc);
}

bool Syscalls::simulate_vsync() {
    bool switched = false;

    // Auto-start the scheduler: on the first vsync after tasks are registered,
    // perform a real context switch to the highest-priority task.
    // The caller (AppMain / dl_main) has finished its setup work by this point;
    // we save its context as idle so OSTaskDel can fall back to it if needed.
    if (m_current_task < 0 && m_task_count > 0 && !m_scheduler_started) {
        int highest = -1;
        u8 best_prio = 255;
        for (int i = 0; i < m_task_count; i++) {
            if (m_tasks[i].active && !m_tasks[i].blocked && m_tasks[i].task_prio < best_prio) {
                highest = i;
                best_prio = m_tasks[i].task_prio;
            }
        }
        if (highest >= 0) {
            memcpy(m_idle_regs, g_cpu_regs, sizeof(g_cpu_regs));
            m_idle_pc = g_cpu_pc;
            m_idle_hi = g_cpu_hi;
            m_idle_lo = g_cpu_lo;

            m_scheduler_started = true;
            switch_to_task(highest);
            printf("[SCHEDULER] Starting task %d (prio %u) at 0x%08X\n",
                   highest, best_prio, g_cpu_pc);
            return true;
        }
    }

    // µC/OS-II tick counter at 100 Hz (standard OS_TICKS_PER_SEC default on JZ4740).
    // Real firmware drives this via a hardware timer ISR at 100 Hz.
    u32 now = SDL_GetTicks();
    u32 expected_ticks = (now - m_start_tick) * 100 / 1000;
    while (m_os_ticks < expected_ticks) {
        m_os_ticks++;
        for (int i = 0; i < m_task_count; i++) {
            if (m_tasks[i].blocked && m_tasks[i].wake_tick > 0 && m_os_ticks >= m_tasks[i].wake_tick) {
                if (m_tasks[i].block_sem) {
                    m_tasks[i].regs[2] = 10;  // OS_TIMEOUT in v0
                    if (m_tasks[i].sem_err_ptr)
                        m_mem.write_u8(m_tasks[i].sem_err_ptr, 10);  // OS_TIMEOUT
                    m_tasks[i].sem_err_ptr = 0;
                    m_tasks[i].block_sem = 0;
                }
                m_tasks[i].blocked = false;
                m_tasks[i].wake_tick = 0;
            }
        }
    }

    if (m_audio_space_flag.exchange(false, std::memory_order_relaxed))
        audio_wake_waiters();

    // Cooperative multitasking: yield to another ready task if available.
    // The game uses tasks as inline-synchronized call chains (not independent
    // contexts), so there's typically nothing to switch to. This mostly keeps
    // current-task bookkeeping consistent.
    if (m_current_task >= 0 && m_current_task < m_task_count) {
        Task& current = m_tasks[m_current_task];
        if (current.blocked) {
            // Current task blocked — try to find a ready task
            save_current_task();
            int next = find_ready_task();
            if (next >= 0) {
                switch_to_task(next);
                switched = true;
            }
        } else {
            // Current task still running — yield to higher-priority task if any
            save_current_task();
            int next = find_ready_task();
            if (next >= 0 && next != m_current_task) {
                switch_to_task(next);
                switched = true;
            }
        }
    } else {
        int next = find_ready_task();
        if (next >= 0) {
            switch_to_task(next);
            switched = true;
            if (m_in_idle) {
                printf("[SCHEDULER] Leaving idle, switching to task %d\n", next);
                m_in_idle = false;
            }
        }
    }

    // Mixers (prio >= 16) pace by blocking inside waveout_write. After the
    // ring has space they are ready again, but AppMain (prio 5) is also ready
    // and a priority-only switch starves them — Hell Striker II plays the
    // first fill then goes silent. Resume a ready audio worker as if
    // waveout_write were a blocking device wait. Skip --nosound: writes
    // never block, so a forced slice would spin the mixer forever.
    if (!m_nosound && (m_current_task < 0
                       || (m_current_task < m_task_count
                           && m_tasks[m_current_task].task_prio < 16))) {
        int audio = -1;
        u8 best = 255;
        for (int i = 0; i < m_task_count; i++) {
            if (m_tasks[i].active && !m_tasks[i].blocked
                && m_tasks[i].task_prio >= 16 && m_tasks[i].task_prio < best) {
                best = m_tasks[i].task_prio;
                audio = i;
            }
        }
        if (audio >= 0) {
            if (m_current_task >= 0)
                save_current_task();
            switch_to_task(audio);
            switched = true;
            m_in_idle = false;
        }
    }

    // Keep SDL window alive without triggering frame-count dirty flag.
    m_display.present_blank();

    // Periodic GOT call dump every 1000 frames
    static u32 dump_count = 0;
    dump_count++;
    if (dump_count % 1000 == 0) {
        for (int i = 0; i < (int)MAX_GOT_ENTRIES; i++) {
            if (m_got_call_counts[i] > 0) {
                printf("[GOT] %3d: %-25s %u\n", i, got_name(i), m_got_call_counts[i]);
            }
        }
    }
    return switched;
}

// ── Stubs for remaining Dingoo OS APIs ───────────────────────────────────

void Syscalls::impl_Custom_Memsic_test()    { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_GUI_TIMER_Exec()        { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_Get_X()                 { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_Get_Y()                 { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_Memsic_SerialCommInit() { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_Read_Acc()              { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_Read_Acc0()             { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_SysDisableBkLight()     { printf("[STUB] %s\n", __func__); }
void Syscalls::impl__tcscmp()               { printf("[STUB] %s\n", __func__); }
void Syscalls::impl__tcscpy()               { printf("[STUB] %s\n", __func__); }
void Syscalls::impl__waveout_open()         { impl_waveout_open(); }
void Syscalls::impl__waveout_set_volume()   { impl_waveout_set_volume(); }
void Syscalls::impl_av_begin_thread()       { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_av_create_flag()        { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_av_create_sem()         { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_av_delay()              { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_av_destroy_flag()       { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_av_destroy_sem()        { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_av_end_thread()         { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_av_give_flag()          { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_av_give_sem()           { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_av_queue_abort()        { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_av_queue_end()          { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_av_queue_flush()        { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_av_queue_get()          { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_av_queue_init()         { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_av_queue_put()          { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_av_reg_object()         { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_av_resize_packet()      { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_av_uft8_2_unicode()     { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_av_unreg_object()       { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_av_upper_4cc()          { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_av_wait_flag()          { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_av_wait_sem()           { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_av_wait_sem2()          { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_delay_ms()              { (void)arg(0); g_cpu_regs[2] = 0; }
void Syscalls::impl_detect_clock()          { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_fsys_fcloseW()          { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_fsys_fclose_flash()     { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_fsys_fopen_flash()      { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_fsys_mkdir()            { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_fsys_removeW()          { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_fsys_renameW()          { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_isTVON()                { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_memcpy()                { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_memset()                { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_serial_puts()           { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_sscanf()                { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_sys_get_ccpmp_config()  { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_tv_close()              { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_tv_disable_switch()     { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_tv_enable_switch()      { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_tv_get_closeflag()      { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_tv_get_openflag()       { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_tv_open()               { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_tv_set_closeflag()      { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_tv_set_openflag()       { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_udelay()                { (void)arg(0); g_cpu_regs[2] = 0; }
void Syscalls::impl_vsprintf()              { printf("[STUB] %s\n", __func__); }
