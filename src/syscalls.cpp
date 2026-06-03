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
    {"Custom_Memsic_test",      &Syscalls::impl_Custom_Memsic_test,      true},
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
    {"fsys_clearerr",           &Syscalls::impl_fsys_clearerr,           true},
    {"fsys_fclose",             &Syscalls::impl_fsys_fclose,             false},
    {"fsys_fclose_flash",       &Syscalls::impl_fsys_fclose_flash,       true},
    {"fsys_fcloseW",            &Syscalls::impl_fsys_fcloseW,            true},
    {"fsys_feof",               &Syscalls::impl_fsys_feof,               false},
    {"fsys_ferror",             &Syscalls::impl_fsys_ferror,             false},
    {"fsys_findclose",          &Syscalls::impl_fsys_findclose,          true},
    {"fsys_findfirst",          &Syscalls::impl_fsys_findfirst,          true},
    {"fsys_findnext",           &Syscalls::impl_fsys_findnext,           true},
    {"fsys_flush_cache",        &Syscalls::impl_fsys_flush_cache,        true},
    {"fsys_fopen",              &Syscalls::impl_fsys_fopen,              false},
    {"fsys_fopen_flash",        &Syscalls::impl_fsys_fopen_flash,        true},
    {"fsys_fopenW",             &Syscalls::impl_fsys_fopenW,             false},
    {"fsys_fread",              &Syscalls::impl_fsys_fread,              false},
    {"fsys_fseek",              &Syscalls::impl_fsys_fseek,              false},
    {"fsys_ftell",              &Syscalls::impl_fsys_ftell,              false},
    {"fsys_fwrite",             &Syscalls::impl_fsys_fwrite,             false},
    {"fsys_mkdir",              &Syscalls::impl_fsys_mkdir,              true},
    {"fsys_RefreshCache",       &Syscalls::impl_fsys_RefreshCache,       true},
    {"fsys_remove",             &Syscalls::impl_fsys_remove,             false},
    {"fsys_removeW",            &Syscalls::impl_fsys_removeW,            true},
    {"fsys_rename",             &Syscalls::impl_fsys_rename,             false},
    {"fsys_renameW",            &Syscalls::impl_fsys_renameW,            true},
    {"fwrite",                  &Syscalls::impl_fwrite,                  false},
    {"get_current_language",    &Syscalls::impl_get_current_language,    false},
    {"get_dl_handle",           &Syscalls::impl_get_dl_handle,           false},
    {"get_game_vol",            &Syscalls::impl_get_game_vol,            false},
    {"Get_X",                   &Syscalls::impl_Get_X,                   true},
    {"Get_Y",                   &Syscalls::impl_Get_Y,                   true},
    {"GetTickCount",            &Syscalls::impl_GetTickCount,            false},
    {"GUI_Exec",                &Syscalls::impl_GUI_Exec,                true},
    {"GUI_Lock",                &Syscalls::impl_GUI_Lock,                false},
    {"GUI_TIMER_Create",        &Syscalls::impl_GUI_TIMER_Create,        true},
    {"GUI_TIMER_Delete",        &Syscalls::impl_GUI_TIMER_Delete,        true},
    {"GUI_TIMER_Exec",          &Syscalls::impl_GUI_TIMER_Exec,          true},
    {"GUI_TIMER_Restart",       &Syscalls::impl_GUI_TIMER_Restart,       true},
    {"GUI_TIMER_SetPeriod",     &Syscalls::impl_GUI_TIMER_SetPeriod,     true},
    {"GUI_Unlock",              &Syscalls::impl_GUI_Unlock,              false},
    {"HP_Mute_sw",              &Syscalls::impl_HP_Mute_sw,              true},
    {"isTVON",                  &Syscalls::impl_isTVON,                  true},
    {"jz_pm_pllconvert",        &Syscalls::impl_jz_pm_pllconvert,        true},
    {"kbd_get_key",             &Syscalls::impl_kbd_get_key,             false},
    {"kbd_get_status",          &Syscalls::impl_kbd_get_status,          false},
    {"LCD_Color2Index",         &Syscalls::impl_LCD_Color2Index,         false},
    {"lcd_flip",                &Syscalls::impl_lcd_flip,                false},
    {"lcd_get_bpp",             &Syscalls::impl_lcd_get_bpp,             false},
    {"lcd_get_cframe",          &Syscalls::impl_lcd_get_cframe,          false},
    {"lcd_get_frame",           &Syscalls::impl_lcd_get_frame,           false},
    {"LCD_GetXSize",            &Syscalls::impl_LCD_GetXSize,            false},
    {"LCD_GetYSize",            &Syscalls::impl_LCD_GetYSize,            false},
    {"lcd_set_frame",           &Syscalls::impl_lcd_set_frame,           false},
    {"LcdGetDisMode",           &Syscalls::impl_LcdGetDisMode,           false},
    {"malloc",                  &Syscalls::impl_malloc,                  false},
    {"mdelay",                  &Syscalls::impl_mdelay,                  false},
    {"memcpy",                  &Syscalls::impl_memcpy,                  true},
    {"memset",                  &Syscalls::impl_memset,                  true},
    {"Memsic_SerialCommInit",   &Syscalls::impl_Memsic_SerialCommInit,   true},
    {"open_gui_key_msg",        &Syscalls::impl_open_gui_key_msg,        true},
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
    {"pcm_can_write",           &Syscalls::impl_pcm_can_write,           false},
    {"pcm_ioctl",               &Syscalls::impl_pcm_ioctl,               false},
    {"printf",                  &Syscalls::impl_printf,                  false},
    {"Read_Acc",                &Syscalls::impl_Read_Acc,                true},
    {"Read_Acc0",               &Syscalls::impl_Read_Acc0,               true},
    {"realloc",                 &Syscalls::impl_realloc,                 false},
    {"serial_getc",             &Syscalls::impl_serial_getc,             true},
    {"serial_putc",             &Syscalls::impl_serial_putc,             false},
    {"serial_puts",             &Syscalls::impl_serial_puts,             true},
    {"spin_lock_irqsave",       &Syscalls::impl_spin_lock_irqsave,       true},
    {"spin_unlock_irqrestore",  &Syscalls::impl_spin_unlock_irqrestore,  true},
    {"sprintf",                 &Syscalls::impl_sprintf,                 false},
    {"sscanf",                  &Syscalls::impl_sscanf,                  true},
    {"StartSwTimer",            &Syscalls::impl_StartSwTimer,            false},
    {"strlen",                  &Syscalls::impl_strlen,                  false},
    {"strncasecmp",             &Syscalls::impl_strncasecmp,             false},
    {"sys_get_ccpmp_config",    &Syscalls::impl_sys_get_ccpmp_config,    true},
    {"sys_judge_event",         &Syscalls::impl_sys_judge_event,         false},
    {"SysDisableBkLight",       &Syscalls::impl_SysDisableBkLight,       true},
    {"SysDisableCloseBkLight",  &Syscalls::impl_SysDisableCloseBkLight,  true},
    {"SysEnableShutDownPower",  &Syscalls::impl_SysEnableShutDownPower,  true},
    {"TaskMediaFunStop",        &Syscalls::impl_TaskMediaFunStop,        true},
    {"tv_close",                &Syscalls::impl_tv_close,                true},
    {"tv_disable_switch",       &Syscalls::impl_tv_disable_switch,       true},
    {"tv_enable_switch",        &Syscalls::impl_tv_enable_switch,        true},
    {"tv_get_closeflag",        &Syscalls::impl_tv_get_closeflag,        true},
    {"tv_get_openflag",         &Syscalls::impl_tv_get_openflag,         true},
    {"tv_open",                 &Syscalls::impl_tv_open,                 true},
    {"tv_set_closeflag",        &Syscalls::impl_tv_set_closeflag,        true},
    {"tv_set_openflag",         &Syscalls::impl_tv_set_openflag,         true},
    {"U8TOU16",                 &Syscalls::impl_U8TOU16,                 true},
    {"U8TOU32",                 &Syscalls::impl_U8TOU32,                 true},
    {"udc_attached",            &Syscalls::impl_udc_attached,            true},
    {"udelay",                  &Syscalls::impl_udelay,                  true},
    {"USB_Connect",             &Syscalls::impl_USB_Connect,             true},
    {"USB_No_Connect",          &Syscalls::impl_USB_No_Connect,          true},
    {"vsprintf",                &Syscalls::impl_vsprintf,                true},
    {"vxGoHome",                &Syscalls::impl_vxGoHome,                true},
    {"waveout_can_write",       &Syscalls::impl_waveout_can_write,       false},
    {"waveout_close",           &Syscalls::impl_waveout_close,           false},
    {"waveout_close_at_once",   &Syscalls::impl_waveout_close_at_once,   false},
    {"waveout_open",            &Syscalls::impl_waveout_open,            false},
    {"waveout_set_volume",      &Syscalls::impl_waveout_set_volume,      true},
    {"waveout_write",           &Syscalls::impl_waveout_write,           false},
    {"WM__SendMessage",         &Syscalls::impl_WM__SendMessage,         true},
    {"WM_CreateWindow",         &Syscalls::impl_WM_CreateWindow,         true},
    {"WM_DefaultProc",          &Syscalls::impl_WM_DefaultProc,          true},
    {"WM_DeleteWindow",         &Syscalls::impl_WM_DeleteWindow,         true},
    {"WM_SelectWindow",         &Syscalls::impl_WM_SelectWindow,         true},
    {"WM_SetFocus",             &Syscalls::impl_WM_SetFocus,             true},
};
    for (u32 mask : priority) {
        if (keys & mask) {
            u32 code = bitmask_to_keycode(mask);
            printf("[INPUT] _kbd_get_key -> 0x%02X (code=%u)\n", code, code);
            g_cpu_regs[2] = code;
            return;
        }
    }
    g_cpu_regs[2] = 0;
}

// === GOT 32-45: filesystem ===

void Syscalls::impl_fsys_fopen() {
    u32 path_addr = arg(0);
    u32 mode_addr = arg(1);
    std::string path = guest_string(path_addr);
    std::string mode = guest_string(mode_addr);

    // Look up in archive first
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

    // Create mutex if not yet created
    if (!m_audio_mutex) {
        m_audio_mutex = SDL_CreateMutex();
        printf("[AUDIO] Created audio mutex: %p\n", (void*)m_audio_mutex);
    }

    if (m_audio_device > 0) {
        SDL_CloseAudioDevice(m_audio_device);
        m_audio_device = 0;
    }

    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq = sample_rate;
    want.format = AUDIO_S16SYS;
    want.channels = (Uint8)channels;
    want.samples = 2048;
    want.callback = audio_callback;
    want.userdata = this;

    m_audio_device = SDL_OpenAudioDevice(nullptr, 0, &want, 0, 0);
    if (m_audio_device > 0) {
        SDL_PauseAudioDevice(m_audio_device, 0);
        m_audio_open = true;
        m_audio_device_open = true;
        printf("[AUDIO] waveout_open: %dHz %dch %dbit -> device=%u (got %dHz %dch)\n",
               sample_rate, channels, bits, m_audio_device, want.freq, want.channels);
        g_cpu_regs[2] = 1;  // return non-zero instance handle
    } else {
        printf("[AUDIO] waveout_open FAILED: %s\n", SDL_GetError());
        m_audio_open = false;
        m_audio_device_open = false;
        g_cpu_regs[2] = 0;
    }
}

void Syscalls::impl_waveout_close() {
    if (m_audio_device > 0) {
        SDL_CloseAudioDevice(m_audio_device);
        m_audio_device = 0;
    }
    m_audio_open = false;
    m_audio_device_open = false;
    SDL_LockMutex(m_audio_mutex);
    while (!m_audio_queue.empty()) m_audio_queue.pop();
    SDL_UnlockMutex(m_audio_mutex);
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_waveout_close_at_once() {
    impl_waveout_close();
}

void Syscalls::impl_waveout_set_volume() {
    // SDK: int waveout_set_volume(waveout_inst* inst, int vol)  vol = 0-100
    u32 vol = arg(1);
    m_volume = (float)vol / 100.0f;
    if (m_volume < 0.0f) m_volume = 0.0f;
    if (m_volume > 1.0f) m_volume = 1.0f;
    printf("[AUDIO] waveout_set_volume(%u) -> %.2f\n", vol, m_volume);
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_HP_Mute_sw() {
    printf("[AUDIO] HP_Mute_sw -> muted\n");
    m_volume = 0.0f;
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_waveout_write() {
    // SDK: int waveout_write(waveout_inst* inst, char* buffer, int count)
    // skip inst (arg0), buffer = arg1, count = arg2
    u32 buf_addr = arg(1);
    u32 size = arg(2);

    if (size == 0 || !buf_addr) {
        g_cpu_regs[2] = 0;
        return;
    }

    // Log first few calls to see actual buffer addresses and sizes
    if (m_audio_write_count < 10) {
        printf("[AUDIO] waveout_write #%u: buf=0x%08X size=%u bytes\n",
               m_audio_write_count, buf_addr, size);
    } else if (m_audio_write_count == 10) {
        printf("[AUDIO] waveout_write: subsequent calls suppressed\n");
    }

    const u32 count = size / 2;
    // Limit total queue size to 8192 samples (≈16 KB) to avoid unbounded growth.
    constexpr size_t MAX_QUEUE_SAMPLES = 8192;
    std::vector<s16> samples(count);
    m_mem.read_block(buf_addr, (u8*)samples.data(), size);

    SDL_LockMutex(m_audio_mutex);
    size_t space = (MAX_QUEUE_SAMPLES > m_audio_queue.size()) ? (MAX_QUEUE_SAMPLES - m_audio_queue.size()) : 0;
    size_t to_push = std::min<size_t>(count, space);
    for (size_t i = 0; i < to_push; ++i) {
        s16 s = (s16)(samples[i] * m_volume);
        m_audio_queue.push(s);
    }
    SDL_UnlockMutex(m_audio_mutex);

    m_audio_write_count++;

    // Throttle: block the calling task for the real-time duration of the audio buffer.
    // Real DMA hardware blocks until there is space; without this the audio task runs
    // ~200× faster than real time and signals AppMain to exit prematurely.
    if (m_scheduler_started && m_current_task >= 0 && m_audio_sample_rate > 0) {
        // Number of PCM frames (per-channel samples) in this buffer
        u32 ch         = (m_audio_channels > 0) ? m_audio_channels : 2;
        u32 pcm_frames = size / (ch * 2u);   // 2 bytes per s16 sample
        // Convert to OS ticks at ~60 Hz (vsync rate used by the scheduler)
        u32 delay_ticks = (pcm_frames * 60u) / m_audio_sample_rate;
        if (delay_ticks < 1) delay_ticks = 1;

        save_current_task();
        m_tasks[m_current_task].blocked   = true;
        m_tasks[m_current_task].wake_tick = m_os_ticks + delay_ticks;
        m_tasks[m_current_task].regs[2]   = size;  // return value when task resumes

        int next = find_ready_task();
        if (next >= 0) {
            switch_to_task(next);
        } else {
            // No other ready task: signal the main loop to tick
            m_task_switched = true;
        }
        return;
    }

    g_cpu_regs[2] = size; // return byte count (non-scheduler path)
}

void Syscalls::impl_waveout_can_write() {
    // Return available write space in bytes (max 8192 samples, subtract queued)
    constexpr size_t MAX_QUEUE_SAMPLES = 8192;
    SDL_LockMutex(m_audio_mutex);
    size_t queued = m_audio_queue.size();
    SDL_UnlockMutex(m_audio_mutex);
    size_t free_samples = (queued >= MAX_QUEUE_SAMPLES) ? 0 : (MAX_QUEUE_SAMPLES - queued);
    u32 free_bytes = static_cast<u32>(free_samples * 2);
    // Return at least a small buffer so callers don't think audio is dead.
    g_cpu_regs[2] = free_bytes ? free_bytes : 4096;
}

void Syscalls::impl_pcm_can_write() {
    impl_waveout_can_write();
}

void Syscalls::impl_pcm_ioctl() {
    // pcm_ioctl(cmd, arg) — Dingoo SDK signature
    u32 cmd = arg(0);
    u32 arg_val = arg(1);
    (void)arg_val; // not all cmds use arg_val

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
        // arg is pointer to int; write available write space
        constexpr size_t MAX_QUEUE_SAMPLES = 8192;
        SDL_LockMutex(m_audio_mutex);
        size_t queued = m_audio_queue.size();
        SDL_UnlockMutex(m_audio_mutex);
        u32 free_bytes = static_cast<u32>((queued >= MAX_QUEUE_SAMPLES) ? 0 : (MAX_QUEUE_SAMPLES - queued) * 2);
        if (free_bytes == 0) free_bytes = 4096; // never return 0 so audio loops don't busy-spin
        if (arg_val) m_mem.write_u32(arg_val, free_bytes);
        printf("[PCM] ioctl GET_SPACE -> %u bytes\n", free_bytes);
        g_cpu_regs[2] = 0; // success
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


void Syscalls::impl_OSSemPend() {
    u32 sem_ptr = arg(0);
    u32 timeout = arg(1);  // 0 = wait forever
    u32 err_ptr = arg(2);

    u16 cnt = m_mem.read_u16(sem_ptr + 8);  // OSEventCnt at +8 (16-bit)

    if (cnt > 0) {
        m_mem.write_u16(sem_ptr + 8, (u16)(cnt - 1));
        if (err_ptr) m_mem.write_u8(err_ptr, 0);  // OS_NO_ERR
        return;
    }

    // No current task (e.g. after OSTaskDel self) — can't block, return timeout.
    if (m_current_task < 0 || m_current_task >= m_task_count) {
        if (err_ptr) m_mem.write_u8(err_ptr, 10);  // OS_TIMEOUT
        g_cpu_regs[2] = 10;
        return;
    }

    // Block current task on this semaphore.
    // *err is written when the task resumes (by OSSemPost or by timeout in simulate_vsync).
    save_current_task();
    m_tasks[m_current_task].blocked = true;
    m_tasks[m_current_task].block_sem = sem_ptr;
    m_tasks[m_current_task].sem_err_ptr = err_ptr;
    if (timeout > 0) {
        m_tasks[m_current_task].wake_tick = m_os_ticks + timeout;
    } else {
        m_tasks[m_current_task].wake_tick = 0;  // wait forever
    }

    int next = find_ready_task();
    if (next >= 0) {
        switch_to_task(next);
    } else {
        // No other ready task - unblock and return OS_TIMEOUT to caller.
        m_tasks[m_current_task].blocked = false;
        m_tasks[m_current_task].block_sem = 0;
        m_tasks[m_current_task].sem_err_ptr = 0;
        m_tasks[m_current_task].wake_tick = 0;
        if (err_ptr) m_mem.write_u8(err_ptr, 10);  // OS_TIMEOUT
        g_cpu_regs[2] = 10;  // return OS_TIMEOUT in v0
    }
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
    } else {
        // No other task ready — yield to idle loop until vsync wakes this task
        memcpy(g_cpu_regs, m_idle_regs, sizeof(g_cpu_regs));
        g_cpu_pc = m_idle_pc;
        g_cpu_hi = m_idle_hi;
        g_cpu_lo = m_idle_lo;
        m_task_switched = true;
    }
    g_cpu_regs[2] = 0;
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

    // The value at EVENT_QUEUE_ADDR (0x80BFECD8) is pre-populated with the
    // hardware-ready sentinel 0x8BFC4D89 for the game to read directly during
    // audio init. Clear it silently when _sys_judge_event sees it — the game
    // interprets 0x8BFC4D89 as an OS exit signal, causing premature shutdown.
    u32 event_val = m_mem.read_u32(EVENT_QUEUE_ADDR);
    if (event_val == 0x8BFC4D89u) {
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

    // Look up in archive first — try multiple path variants
    if (m_archive) {
        const ArchiveEntry* entry = m_archive->find(search_path);
        // Try stripping .\  prefix (PC format: archive has bare names)
        if (!entry && search_path.size() > 2 && search_path[0] == '.' &&
            (search_path[1] == '\\' || search_path[1] == '/'))
            entry = m_archive->find(search_path.substr(2));
        // Try stripping res\ prefix
        if (!entry && search_path.size() > 4 &&
            (search_path.substr(0, 4) == "res\\" || search_path.substr(0, 4) == "res/"))
            entry = m_archive->find(search_path.substr(4));
        // Try bare filename (after last slash/backslash)
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

// === dl_res resource API (brick.app only) ===

int Syscalls::alloc_dl_res_handle() {
    for (int i = 0; i < MAX_DL_RES; i++) {
        if (!m_dl_res[i].in_use) {
            m_dl_res[i].in_use = true;
            return i;
        }
    }
    return -1;
}

void Syscalls::free_dl_res_handle(int idx) {
    if (idx >= 0 && idx < MAX_DL_RES) {
        m_dl_res[idx].in_use = false;
    }
}

void Syscalls::impl_get_dl_handle() {
    // dl_load stores module metadata and returns a handle — that return value IS the handle.
    // Since dl_res_* functions use the Archive directly (not module handles), we return a
    // dummy non-zero handle.  The real implementation would look up the calling module's
    // entry in the module resource database at 0x8057F168+ (12-byte entries with 24-bit keys),
    // but the OS never wired this GOT slot up (func pointer is NULL in the binary).
    u32 handle = m_dl_handle_counter;
    if (handle == 0) { handle = 1; m_dl_handle_counter = 2; }
    else m_dl_handle_counter++;
    printf("[DL] get_dl_handle -> %u\n", handle);
    g_cpu_regs[2] = handle;
}

void Syscalls::impl_dl_res_open() {
    // a0 = resource name (ANSI string)
    u32 name_addr = arg(0);
    if (name_addr == 0 || !m_archive) {
        g_cpu_regs[2] = 0;
        return;
    }
    std::string name = m_mem.read_string(name_addr);
    if (name.empty()) { g_cpu_regs[2] = 0; return; }

    // Try exact match, then bare filename
    const ArchiveEntry* entry = m_archive->find(name);
    if (!entry) {
        // Try stripping .\ and res\ prefixes, then bare filename
        if (name.size() > 2 && name[0] == '.' && (name[1] == '\\' || name[1] == '/'))
            entry = m_archive->find(name.substr(2));
        if (!entry && name.size() > 4 &&
            (name.substr(0, 4) == "res\\" || name.substr(0, 4) == "res/"))
            entry = m_archive->find(name.substr(4));
        if (!entry) {
            size_t slash = name.find_last_of("/\\");
            if (slash != std::string::npos)
entry = m_archive->find(name.substr(slash + 1));
        }
    }
    if (!entry) {
        printf("[dl_res_open] '%s' -> NOT FOUND\n", name.c_str());
        g_cpu_regs[2] = 0;
        return;
    }

    int h = alloc_dl_res_handle();
    if (h < 0) {
        printf("[dl_res_open] '%s' -> out of handles\n", name.c_str());
        g_cpu_regs[2] = 0;
        return;
    }
    m_dl_res[h].entry = entry;
    g_cpu_regs[2] = (u32)(h + 1);
    printf("[dl_res_open] '%s' -> handle %d (size=%u)\n",
           name.c_str(), h + 1, entry->size);
}

void Syscalls::impl_dl_res_get_size() {
    // a0 = handle (1-based, as returned by dl_res_open)
    u32 handle = arg(0);
    if (handle == 0) { g_cpu_regs[2] = 0; return; }
    int idx = (int)handle - 1;
    if (idx < 0 || idx >= MAX_DL_RES || !m_dl_res[idx].in_use || !m_dl_res[idx].entry) {
        g_cpu_regs[2] = 0;
        return;
    }
    g_cpu_regs[2] = m_dl_res[idx].entry->size;
}

void Syscalls::impl_dl_res_get_data() {
    // a0 = handle (1-based), a1 = buf, a2 = offset, a3 = size
    u32 handle = arg(0);
    u32 buf = arg(1);
    u32 offset = arg(2);
    u32 size = arg(3);
    if (handle == 0 || buf == 0 || size == 0) {
        g_cpu_regs[2] = (u32)-1;
        return;
    }
    int idx = (int)handle - 1;
    if (idx < 0 || idx >= MAX_DL_RES || !m_dl_res[idx].in_use || !m_dl_res[idx].entry) {
        g_cpu_regs[2] = (u32)-1;
        return;
    }
    const ArchiveEntry& entry = *m_dl_res[idx].entry;
    if (offset >= entry.size) { g_cpu_regs[2] = 0; return; }
    u32 actual = std::min(size, entry.size - offset);
    m_mem.write_block(buf, m_archive->get_data(entry) + offset, actual);
    g_cpu_regs[2] = actual;
}

void Syscalls::impl_dl_res_close() {
    // a0 = handle (1-based)
    u32 handle = arg(0);
    if (handle == 0) { g_cpu_regs[2] = 0; return; }
    int idx = (int)handle - 1;
    if (idx >= 0 && idx < MAX_DL_RES) {
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
    // Sleep for arg(0) milliseconds
    u32 ms = arg(0);
    if (ms > 0) SDL_Delay(ms);
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

    // µC/OS-II tick counter (one tick per vsync ≈ 60 Hz).
    // The game uses OSTimeGet for animation timing, so ticks must advance
    // at display-frame rate even when the emulator runs faster than real-time.
    m_os_ticks += 1;

    // Wake tasks whose OSTimeDly or OSSemPend timeout has expired.
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

// ── Stub implementations for remaining OS APIs ──────────────────────────

void Syscalls::impl_Custom_Memsic_test() { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_GUI_TIMER_Exec()     { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_Get_X()              { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_Get_Y()              { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_Memsic_SerialCommInit() { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_Read_Acc()           { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_Read_Acc0()          { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_SysDisableBkLight()  { printf("[STUB] %s\n", __func__); }
void Syscalls::impl__tcscmp()            { printf("[STUB] %s\n", __func__); }
void Syscalls::impl__tcscpy()            { printf("[STUB] %s\n", __func__); }
void Syscalls::impl__waveout_open()      { printf("[STUB] %s\n", __func__); }
void Syscalls::impl__waveout_set_volume(){ printf("[STUB] %s\n", __func__); }
void Syscalls::impl_av_begin_thread()    { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_av_create_flag()     { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_av_create_sem()      { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_av_delay()           { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_av_destroy_flag()    { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_av_destroy_sem()     { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_av_end_thread()      { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_av_give_flag()       { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_av_give_sem()        { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_av_queue_abort()     { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_av_queue_end()       { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_av_queue_flush()     { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_av_queue_get()       { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_av_queue_init()      { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_av_queue_put()       { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_av_reg_object()      { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_av_resize_packet()   { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_av_uft8_2_unicode()  { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_av_unreg_object()    { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_av_upper_4cc()       { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_av_wait_flag()       { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_av_wait_sem()        { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_av_wait_sem2()       { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_delay_ms()           { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_detect_clock()       { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_dl_get_proc()        { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_fsys_fcloseW()       { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_fsys_fclose_flash()  { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_fsys_fopen_flash()   { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_fsys_mkdir()         { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_fsys_removeW()       { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_fsys_renameW()       { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_isTVON()             { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_memcpy()             { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_memset()             { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_serial_puts()        { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_sscanf()             { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_sys_get_ccpmp_config(){ printf("[STUB] %s\n", __func__); }
void Syscalls::impl_tv_close()           { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_tv_disable_switch()  { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_tv_enable_switch()   { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_tv_get_closeflag()   { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_tv_get_openflag()    { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_tv_open()            { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_tv_set_closeflag()   { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_tv_set_openflag()    { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_udelay()             { printf("[STUB] %s\n", __func__); }
void Syscalls::impl_vsprintf()           { printf("[STUB] %s\n", __func__); }
