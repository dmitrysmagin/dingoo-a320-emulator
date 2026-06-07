#include "syscalls.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <sys/stat.h>

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
    {"GUI_Exec",                &Syscalls::impl_GUI_Exec,                true},
    {"GUI_Lock",                &Syscalls::impl_GUI_Lock,                false},
    {"GUI_TIMER_Create",        &Syscalls::impl_GUI_TIMER_Create,        true},
    {"GUI_TIMER_Delete",        &Syscalls::impl_GUI_TIMER_Delete,        true},
    {"GUI_TIMER_Exec",          &Syscalls::impl_GUI_TIMER_Exec,          true},
    {"GUI_TIMER_Restart",       &Syscalls::impl_GUI_TIMER_Restart,       true},
    {"GUI_TIMER_SetPeriod",     &Syscalls::impl_GUI_TIMER_SetPeriod,     true},
    {"GUI_Unlock",              &Syscalls::impl_GUI_Unlock,              false},
    {"GetTickCount",            &Syscalls::impl_GetTickCount,            false},
    {"Get_X",                   &Syscalls::impl_Get_X,                   true},
    {"Get_Y",                   &Syscalls::impl_Get_Y,                   true},
    {"HP_Mute_sw",              &Syscalls::impl_HP_Mute_sw,              true},
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
    {"U8TOU16",                 &Syscalls::impl_U8TOU16,                 true},
    {"U8TOU32",                 &Syscalls::impl_U8TOU32,                 true},
    {"USB_Connect",             &Syscalls::impl_USB_Connect,             true},
    {"USB_No_Connect",          &Syscalls::impl_USB_No_Connect,          true},
    {"WM_CreateWindow",         &Syscalls::impl_WM_CreateWindow,         true},
    {"WM_DefaultProc",          &Syscalls::impl_WM_DefaultProc,          true},
    {"WM_DeleteWindow",         &Syscalls::impl_WM_DeleteWindow,         true},
    {"WM_SelectWindow",         &Syscalls::impl_WM_SelectWindow,         true},
    {"WM_SetFocus",             &Syscalls::impl_WM_SetFocus,             true},
    {"WM__SendMessage",         &Syscalls::impl_WM__SendMessage,         true},
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
    {"_waveout_open",           &Syscalls::impl__waveout_open,           true},
    {"_waveout_set_volume",     &Syscalls::impl__waveout_set_volume,     true},
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
    {"cmGetSysModel",           &Syscalls::impl_cmGetSysModel,           true},
    {"cmGetSysVersion",         &Syscalls::impl_cmGetSysVersion,         true},
    {"delay_ms",                &Syscalls::impl_delay_ms,                true},
    {"detect_clock",            &Syscalls::impl_detect_clock,            true},
    {"dl_free",                 &Syscalls::impl_dl_free,                 true},
    {"dl_get_proc",             &Syscalls::impl_dl_get_proc,             true},
    {"dl_load",                 &Syscalls::impl_dl_load,                 true},
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
    {"get_dl_handle",           &Syscalls::impl_get_dl_handle,           true},
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
    {"open_gui_key_msg",        &Syscalls::impl_open_gui_key_msg,        true},
    {"pcm_can_write",           &Syscalls::impl_pcm_can_write,           false},
    {"pcm_ioctl",               &Syscalls::impl_pcm_ioctl,               false},
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
    {"waveout_open",            &Syscalls::impl_waveout_open,            false},
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
    , m_audio_device(0)
    , m_audio_mutex(nullptr)
    , m_ring_write(0)
    , m_ring_read(0)
    , m_volume(1.0f)
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
{
    memset(m_got_call_counts, 0, sizeof(m_got_call_counts));
    for (int i = 0; i < MAX_TASKS; i++) {
        m_tasks[i].active = false;
        m_tasks[i].wake_tick = 0;
        m_tasks[i].block_sem = 0;
        m_tasks[i].sem_err_ptr = 0;
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
    for (int i = 0; i < MAX_DL_RES; i++)
        m_dl_res[i].in_use = false;
    m_archive = nullptr;
    m_audio_open = false;
}

void Syscalls::shutdown_audio() {
    if (m_audio_device > 0) {
        SDL_CloseAudioDevice(m_audio_device);
        m_audio_device = 0;
    }
    if (m_audio_mutex) {
        SDL_LockMutex(m_audio_mutex);
        m_ring_write = m_ring_read = 0;
        SDL_UnlockMutex(m_audio_mutex);
        SDL_DestroyMutex(m_audio_mutex);
        m_audio_mutex = nullptr;
    }
    m_audio_open = false;
    m_audio_device_open = false;
    if (SDL_WasInit(SDL_INIT_AUDIO))
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void SDLCALL Syscalls::audio_callback(void* userdata, Uint8* stream, int len) {
    Syscalls* sys = static_cast<Syscalls*>(userdata);
    if (!sys || !sys->m_audio_mutex) return;
    s16* buf = reinterpret_cast<s16*>(stream);
    int samples = len / 2;
    SDL_LockMutex(sys->m_audio_mutex);
    for (int i = 0; i < samples; i++) {
        if (sys->m_ring_write != sys->m_ring_read) {
            buf[i] = sys->m_ring_buf[sys->m_ring_read];
            sys->m_ring_read = (sys->m_ring_read + 1) % AUDIO_RING_CAP;
        } else {
            buf[i] = 0;  // underrun — ring empty
        }
    }
    SDL_UnlockMutex(sys->m_audio_mutex);
}


u32 Syscalls::arg(int n) {
    if (n >= 0 && n <= 3) return g_cpu_regs[4 + n];
    u32 sp = g_cpu_regs[29];
    return m_mem.read_u32(sp + 16 + (u32)n * 4);
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

void Syscalls::dispatch(int got_index, u32 /*return_addr*/) {
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

u32 Syscalls::heap_alloc(u32 size) {
    if (size == 0) size = 1;
    size = (size + 7) & ~7;
    for (auto& block : m_heap) {
        if (block.free && block.size >= size) {
            block.free = false;
            return block.addr | 0x80000000u;  // return KSEG0 virt, same as fresh alloc
        }
    }
    // Zone 1: 0x00020000–0x009FFFFC (below game binary at 0x00A00000)
    // Zone 2: 0x00C10000–0x01FFFFFC (above stack area, within 32 MB)
    if (m_heap_top + size > 0x009FFFFC && m_heap_top < 0x00C10000)
        m_heap_top = 0x00C10000;
    u32 addr = m_heap_top;
    m_heap_top += size;
    if (m_heap_top > 0x01FFFFFC) {
        printf("[HEAP] OOM: top=0x%08X size=%u\n", m_heap_top, size);
        return 0;
    }
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
    u32 now = SDL_GetTicks();
    u32 delta = now - m_last_timer_tick;
    m_last_timer_tick = now;

    if (delta == 0) return;

    for (auto& t : m_timers) {
        if (!t.active || t.callback == 0) continue;
        t.elapsed += delta;
        while (t.elapsed >= t.period_ms) {
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

    if (keys)
        printf("[INPUT] _kbd_get_status(a0=0x%08X) hw=0x%08X pressed=0x%08X released=0x%08X\n",
               state_ptr, hw, hw_pressed, hw_released);

    m_mem.write_u32(KERNEL_KEY_STATE_ADDR, keys);
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
        printf("[INPUT] _kbd_get_key -> 0x%04X\n", ev);
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

    if (m_archive) {
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

    // Try host file for write mode
    int idx = alloc_file_handle();
    if (idx < 0) { g_cpu_regs[2] = 0; return; }
    if (mode.find('w') != std::string::npos || mode.find('+') != std::string::npos) {
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

void Syscalls::impl_waveout_open() {
    if (m_nosound) { g_cpu_regs[2] = 1; return; }
    u32 a0 = arg(0), a1 = arg(1), a2 = arg(2);
    printf("[AUDIO] waveout_open raw args: a0=0x%08X a1=0x%08X a2=0x%08X\n", a0, a1, a2);

    // waveout_open takes a single waveout_args* pointer:
    //   struct { u32 sample_rate; u16 format; u8 channel; u8 volume; } // 8 bytes
    int sample_rate, channels, bits;

    // Try reading as struct pointer first (SDK convention)
    if (a0 >= 0x80000000 || (a0 >= 0x1000 && a0 < m_mem.size())) {
        // a0 is likely a pointer to waveout_args struct
        sample_rate = (int)m_mem.read_u32(a0);
        u16 format  = m_mem.read_u16(a0 + 4);
        u8  channel = m_mem.read_u8(a0 + 6);
        u8  volume  = m_mem.read_u8(a0 + 7);
        channels = channel;
        bits = (format == 0) ? 16 : 16;  // format field meaning unclear; always 16-bit PCM
        printf("[AUDIO] struct@0x%08X: rate=%d format=%u ch=%u vol=%u\n",
               a0, sample_rate, format, channel, volume);
    } else {
        // Fallback: individual args (legacy)
        sample_rate = (int)a0;
        channels = (int)a1;
        bits = (int)a2;
    }

    if (sample_rate <= 0) sample_rate = 44100;
    if (channels <= 0 || channels > 2) channels = 2;  // force mono/stereo
    if (bits <= 0) bits = 16;

    // Store for use by waveout_write throttle
    m_audio_sample_rate = (u32)sample_rate;
    m_audio_channels    = (u32)channels;

    // Initialise the SDL audio subsystem on first real open (not done at startup
    // so that --nosound mode never touches the audio driver at all).
    if (!SDL_WasInit(SDL_INIT_AUDIO))
        SDL_InitSubSystem(SDL_INIT_AUDIO);

    // Create mutex if not yet created
    if (!m_audio_mutex) {
        m_audio_mutex = SDL_CreateMutex();
        printf("[AUDIO] Created audio mutex: %p\n", (void*)m_audio_mutex);
    }

    SDL_LockMutex(m_audio_mutex);
    m_ring_write = m_ring_read = 0;
    SDL_UnlockMutex(m_audio_mutex);

    if (m_audio_device > 0) {
        SDL_CloseAudioDevice(m_audio_device);
        m_audio_device = 0;
    }

    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq = sample_rate;
    want.format = AUDIO_S16SYS;
    want.channels = (Uint8)channels;
    want.samples = 4096;
    want.callback = audio_callback;
    want.userdata = this;

    m_audio_device = SDL_OpenAudioDevice(nullptr, 0, &want, 0, 0);
    if (m_audio_device > 0) {
        SDL_PauseAudioDevice(m_audio_device, 0);
        m_audio_open = true;
        m_audio_device_open = true;
        printf("[AUDIO] waveout_open: %dHz %dch %dbit -> device=%u (got %dHz %dch)\n",
               sample_rate, channels, bits, m_audio_device, want.freq, want.channels);
        // Write OS audio state variables so games that read firmware RAM directly
        // see the correct state (enable=1, busy=1, stop=0, state=1).
        m_mem.write_u32(0x80242AE4, 1);
        m_mem.write_u32(0x80242558, 1);
        m_mem.write_u32(0x80242560, 0);
        m_mem.write_u32(0x80242580, 1);
        g_cpu_regs[2] = 1;  // return non-zero instance handle
    } else {
        printf("[AUDIO] waveout_open FAILED: %s\n", SDL_GetError());
        m_audio_open = false;
        m_audio_device_open = false;
        g_cpu_regs[2] = 0;
    }
}

void Syscalls::impl_waveout_close() {
    if (m_nosound) { g_cpu_regs[2] = 0; return; }
    if (m_audio_device > 0) {
        SDL_CloseAudioDevice(m_audio_device);
        m_audio_device = 0;
    }
    m_audio_open = false;
    m_audio_device_open = false;
    SDL_LockMutex(m_audio_mutex);
    m_ring_write = m_ring_read = 0;
    SDL_UnlockMutex(m_audio_mutex);
    // Clear OS audio state variables (enable=0, busy=0, stop=1, state=0).
    m_mem.write_u32(0x80242AE4, 0);
    m_mem.write_u32(0x80242558, 0);
    m_mem.write_u32(0x80242560, 1);
    m_mem.write_u32(0x80242580, 0);
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_waveout_close_at_once() {
    impl_waveout_close();
}

void Syscalls::impl_waveout_set_volume() {
    if (m_nosound) { g_cpu_regs[2] = 0; return; }
    // SDK: int waveout_set_volume(waveout_inst* inst, int vol)  vol = 0-100
    u32 vol = arg(1);
    m_volume = (float)vol / 100.0f;
    if (m_volume < 0.0f) m_volume = 0.0f;
    if (m_volume > 1.0f) m_volume = 1.0f;
    printf("[AUDIO] waveout_set_volume(%u) -> %.2f\n", vol, m_volume);
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_HP_Mute_sw() {
    if (m_nosound) { g_cpu_regs[2] = 0; return; }
    printf("[AUDIO] HP_Mute_sw -> muted\n");
    m_volume = 0.0f;
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_waveout_write() {
    u32 buf_addr = arg(1);
    u32 size = arg(2);

    if (m_nosound) { g_cpu_regs[2] = size; return; }

    if (size == 0 || !buf_addr) {
        g_cpu_regs[2] = 0;
        return;
    }

    u32 samples = size / 2;

    // Log first few calls
    if (m_audio_write_count < 10) {
        printf("[AUDIO] waveout_write #%u: buf=0x%08X size=%u samples=%u\n",
               m_audio_write_count, buf_addr, size, samples);
    } else if (m_audio_write_count == 10) {
        printf("[AUDIO] waveout_write: subsequent calls suppressed\n");
    }

    // Non-blocking push into the fixed ring buffer.
    // If the ring is full the excess is silently dropped — the game should have
    // checked waveout_can_write first, matching real hardware behaviour.
    std::vector<s16> buf(samples);
    m_mem.read_block(buf_addr, (u8*)buf.data(), size);

    SDL_LockMutex(m_audio_mutex);
    int free_samples = (m_ring_read - m_ring_write - 1 + AUDIO_RING_CAP) % AUDIO_RING_CAP;
    int to_push = (samples < (u32)free_samples) ? (int)samples : free_samples;
    for (int i = 0; i < to_push; i++) {
        m_ring_buf[m_ring_write] = (s16)(buf[i] * m_volume);
        m_ring_write = (m_ring_write + 1) % AUDIO_RING_CAP;
    }
    SDL_UnlockMutex(m_audio_mutex);

    m_audio_write_count++;
    g_cpu_regs[2] = (u32)to_push * 2;  // bytes actually written
}

void Syscalls::impl_waveout_can_write() {
    if (m_nosound) { g_cpu_regs[2] = 65536; return; }
    SDL_LockMutex(m_audio_mutex);
    int free_samples = (m_ring_read - m_ring_write - 1 + AUDIO_RING_CAP) % AUDIO_RING_CAP;
    SDL_UnlockMutex(m_audio_mutex);
    g_cpu_regs[2] = (u32)free_samples * 2;  // bytes, matches real hardware semantics
}

void Syscalls::impl_pcm_can_write() {
    impl_waveout_can_write();
}

void Syscalls::impl_pcm_ioctl() {
    // pcm_ioctl(cmd, arg) — Dingoo SDK signature
    u32 cmd = arg(0);
    u32 arg_val = arg(1);
    (void)arg_val; // not all cmds use arg_val

    if (m_nosound) {
        if (cmd == 5 /* PCM_GET_SPACE */) {
            if (arg_val) m_mem.write_u32(arg_val, (u32)(AUDIO_RING_CAP - 1) * 2);
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
        SDL_LockMutex(m_audio_mutex);
        int free_samples = (m_ring_read - m_ring_write - 1 + AUDIO_RING_CAP) % AUDIO_RING_CAP;
        SDL_UnlockMutex(m_audio_mutex);
        u32 free_bytes = (u32)free_samples * 2;
        if (arg_val) m_mem.write_u32(arg_val, free_bytes);
        g_cpu_regs[2] = 0;
        break;
    }
    case PCM_SET_SAMPLE_RATE:
    case PCM_SET_CHANNEL:
    case PCM_SET_FORMAT:
    case PCM_SET_HP_VOL:
    case PCM_SET_PAUSE:
    case PCM_SET_PLAY:
    case PCM_SET_MUTE:
    case PCM_RESET:
        // Accepted silently
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
    if (m_current_task >= 0 && m_current_task < m_task_count) {
        memcpy(m_tasks[m_current_task].regs, g_cpu_regs, sizeof(g_cpu_regs));
        m_tasks[m_current_task].hi  = g_cpu_hi;
        m_tasks[m_current_task].lo  = g_cpu_lo;
        m_tasks[m_current_task].pc  = g_cpu_pc;
    }
}

void Syscalls::switch_to_task(int task_idx) {
    if (task_idx < 0 || task_idx >= m_task_count) return;
    memcpy(g_cpu_regs, m_tasks[task_idx].regs, sizeof(g_cpu_regs));
    g_cpu_hi = m_tasks[task_idx].hi;
    g_cpu_lo = m_tasks[task_idx].lo;
    g_cpu_pc = m_tasks[task_idx].pc;
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
        t.active = true;
        t.blocked = false;
        memset(t.regs, 0, sizeof(t.regs));
        t.task_arg = task_arg;
        t.task_prio = (u8)prio;
        t.wake_tick = 0;
        t.block_sem = 0;
        t.sem_err_ptr = 0;
        t.regs[4] = task_arg;
        u32 sp = stack_top & ~0xF;
        sp &= ~0xF;
        t.regs[29] = sp;
        t.regs[30] = sp;
        t.regs[31] = entry;
        t.pc = entry;  // dedicated resume PC

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

    // Find a waiter first (highest priority = lowest task_prio).
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
        // Hand the resource to the waiter; do NOT increment count.
        m_tasks[waiter].blocked = false;
        m_tasks[waiter].block_sem = 0;
        m_tasks[waiter].wake_tick = 0;
        m_tasks[waiter].regs[2] = 0;  // OS_NO_ERR in v0
        if (m_tasks[waiter].sem_err_ptr)
            m_mem.write_u8(m_tasks[waiter].sem_err_ptr, 0);  // OS_NO_ERR
        m_tasks[waiter].sem_err_ptr = 0;
    } else {
        // No waiter: increment count, saturating at 65535.
        u16 cnt = m_mem.read_u16(sem_ptr + 8);
        if (cnt < 65535u) m_mem.write_u16(sem_ptr + 8, (u16)(cnt + 1));
        else { g_cpu_regs[2] = 51 /* OS_SEM_OVF */; return; }
    }
    g_cpu_regs[2] = 0;  // OS_NO_ERR
}

void Syscalls::impl_OSTimeDly() {
    u32 ticks = arg(0);

    if (ticks == 0) {
        g_cpu_regs[2] = 0;
        return;
    }

    // No current task — nothing to delay, just return.
    if (m_current_task < 0 || m_current_task >= m_task_count) {
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
        // No other task ready — yield to idle loop until vsync wakes this task.
        memcpy(g_cpu_regs, m_idle_regs, sizeof(g_cpu_regs));
        g_cpu_pc = m_idle_pc;
        g_cpu_hi = m_idle_hi;
        g_cpu_lo = m_idle_lo;
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
    g_cpu_regs[2] = (u32)(SDL_GetTicks());
}

void Syscalls::impl__sys_judge_event() {
    u32 a0 = arg(0);

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
        printf("[INPUT] _sys_judge_event(a0=0x%08X) -> 0x%08X (queued event)\n", a0, event_val);
        g_cpu_regs[2] = event_val;
        return;
    }

    // Dequeue edge events (key-down / key-up) first
    if (m_display.has_input_event()) {
        event_val = m_display.pop_input_event();
        u8 type = event_val >> 8;
        u8 code = event_val & 0xFF;
        printf("[INPUT] _sys_judge_event(a0=0x%08X) -> 0x%04X (type=%u code=%u)\n",
               a0, event_val, type, code);
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
            printf("[INPUT] _sys_judge_event held -> 0x%04X (code=%u)\n", event_val, code);
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

    if (m_archive) {
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

// === dl_res resource API (SPK archive by path or 24-bit key) ===

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
}

const ArchiveEntry* Syscalls::resolve_dl_res_entry(u32 key_or_path) {
    if (!m_archive || m_archive->count() == 0)
        return nullptr;

    // Guest path pointer (SDK games pass path in $v0 when $a0==0)
    if (key_or_path >= 0x80000000) {
        std::string path = read_guest_path(key_or_path);
        if (!path.empty()) {
            if (const ArchiveEntry* e = m_archive->find(path))
                return e;
            size_t slash = path.find_last_of("/\\");
            if (slash != std::string::npos) {
                if (const ArchiveEntry* e = m_archive->find(path.substr(slash + 1)))
                    return e;
            }
        }
    }

    // Numeric SPK directory index
    if (key_or_path < m_archive->count())
        return &m_archive->entry(key_or_path);

    return nullptr;
}

void Syscalls::impl_get_dl_handle() {
    // firmware: 2nd API #17 has func=0x00000000 (NULL — never implemented).
    // Any module that imported this and called it would jump to address 0 on real hardware.
    // Return 0 so callers' null-handle guards fire cleanly.
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_dl_res_open() {
    u32 path_ptr = arg(0);
    if (path_ptr == 0)
        path_ptr = g_cpu_regs[2]; // path in $v0 (Landlord and other SDK games)

    const ArchiveEntry* entry = resolve_dl_res_entry(path_ptr);
    if (!entry) {
        if (path_ptr >= 0x80000000) {
            static int fail_log = 0;
            if (fail_log < 8)
                printf("[dl_res] open failed: '%s'\n", read_guest_path(path_ptr).c_str());
            fail_log++;
        }
        g_cpu_regs[2] = 0;
        return;
    }

    int idx = alloc_dl_res_handle();
    if (idx < 0) { g_cpu_regs[2] = 0; return; }

    u32 guest = heap_alloc(entry->size);
    if (!guest) {
        free_dl_res_handle(idx);
        g_cpu_regs[2] = 0;
        return;
    }

    const u8* src = m_archive->get_data(*entry);
    m_mem.write_block(guest, src, entry->size);

    m_dl_res[idx].in_use = true;
    m_dl_res[idx].guest_addr = guest;
    m_dl_res[idx].size = entry->size;

    static int ok_log = 0;
    if (ok_log < 8)
        printf("[dl_res] open '%s' -> handle %d (%u bytes @ 0x%08X)\n",
               entry->name.c_str(), idx + 1, entry->size, guest);
    ok_log++;

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
    if (idx < 0 || idx >= MAX_DL_RES || !m_dl_res[idx].in_use)
        g_cpu_regs[2] = 0;
    else
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
    // Non-underscore wrapper: same as _kbd_get_key
    impl__kbd_get_key();
}

void Syscalls::impl_kbd_get_status() {
    // Non-underscore wrapper: same as _kbd_get_status
    impl__kbd_get_status();
}

void Syscalls::impl_sys_judge_event() {
    // Non-underscore wrapper: same as _sys_judge_event
    impl__sys_judge_event();
}

void Syscalls::impl_open_gui_key_msg() {
    // µC/GUI-specific; only imported by Yi-Chi King Fighter
    printf("[STUB] open_gui_key_msg\n");
    g_cpu_regs[2] = 0;
}

// === Non‑standard GOT app functions (Yi‑Chi, Overlord‑Fighter) ===

void Syscalls::impl_cmGetSysVersion() {
    // Return pointer to version string "V1.0" in guest memory
    static u32 s_version_addr = 0;
    static const char* version = "V1.0";
    if (s_version_addr == 0) {
        // Allocate from a scratch area (above kernel data, safe in KSEG0)
        s_version_addr = 0x80C0FF00;
        for (int i = 0; version[i]; i++)
            m_mem.write_u8(s_version_addr + (u32)i, (u8)version[i]);
        m_mem.write_u8(s_version_addr + (u32)strlen(version), 0);
    }
    g_cpu_regs[2] = s_version_addr;
}

void Syscalls::impl_cmGetSysModel() {
    // Return pointer to model string "A320" in guest memory
    static u32 s_model_addr = 0;
    static const char* model = "A320";
    if (s_model_addr == 0) {
        s_model_addr = 0x80C0FF20;
        for (int i = 0; model[i]; i++)
            m_mem.write_u8(s_model_addr + (u32)i, (u8)model[i]);
        m_mem.write_u8(s_model_addr + (u32)strlen(model), 0);
    }
    g_cpu_regs[2] = s_model_addr;
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

void Syscalls::impl_GUI_TIMER_SetPeriod() {
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_GUI_TIMER_Restart() {
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_GUI_TIMER_Delete() {
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_WM__SendMessage() {
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_WM_DefaultProc() {
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_GUI_TIMER_Create() {
    u32 period = arg(0);
    u32 cb = arg(1);
    u32 context = arg(2);
    printf("[µC/GUI] GUI_TIMER_Create(period=%u cb=0x%08X ctx=0x%08X)\n", period, cb, context);
    g_cpu_regs[2] = 1; // timer handle
}

void Syscalls::impl_WM_SelectWindow() {
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_WM_CreateWindow() {
    // WM_CreateWindow(x0,y0,x1,y1,style,flags,cb,client): returns WM_HWIN
    u32 x0 = arg(0), y0 = arg(1), x1 = arg(2), y1 = arg(3);
    u32 cb = arg(6);
    printf("[µC/GUI] WM_CreateWindow(%u,%u,%u,%u cb=0x%08X)\n", x0, y0, x1, y1, cb);
    g_cpu_regs[2] = 1; // window handle
}

void Syscalls::impl_WM_DeleteWindow() {
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_GUI_Exec() {
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
    // dl_load(name) — load another .app module; return handle
    u32 name_ptr = arg(0);
    std::string name = name_ptr ? guest_string(name_ptr) : "?";
    printf("[DL] dl_load('%s') -> stub\n", name.c_str());
    // On real hardware this loads a separate module; just return a dummy handle
    g_cpu_regs[2] = m_dl_handle_counter++;
}

void Syscalls::impl_dl_free() {
    u32 handle = arg(0);
    printf("[DL] dl_free(%u) -> stub\n", handle);
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_U8TOU16() {
    u32 dst = arg(0);
    u32 src = arg(1);
    u32 len = arg(2);
    if (src && dst) {
        std::string src_str = guest_string(src);
        for (u32 i = 0; i < len && i < src_str.size(); i++) {
            m_mem.write_u16(dst + i * 2, (u16)(u8)src_str[i]);
        }
    }
    printf("[U8TOU16] '%s' -> len=%u\n", src ? guest_string(src).c_str() : "?", len);
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_U8TOU32() {
    u32 dst = arg(0);
    u32 src = arg(1);
    u32 len = arg(2);
    if (src && dst) {
        std::string src_str = guest_string(src);
        for (u32 i = 0; i < len && i < src_str.size(); i++) {
            m_mem.write_u32(dst + i * 4, (u32)(u8)src_str[i]);
        }
    }
    printf("[U8TOU32] '%s' -> len=%u\n", src ? guest_string(src).c_str() : "?", len);
    g_cpu_regs[2] = 0;
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
void Syscalls::impl_dl_get_proc()           { printf("[STUB] %s\n", __func__); }
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
