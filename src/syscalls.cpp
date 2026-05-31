#include "syscalls.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>

extern u32 g_cpu_regs[32];
extern u32 g_cpu_pc;
extern u32 g_cpu_hi;
extern u32 g_cpu_lo;
extern u32 g_detected_fb_addr;  // physical, intercepted from OS at PC 0x80A21E78

Syscalls::Syscalls(Memory& mem, Display& display)
    : m_mem(mem)
    , m_display(display)
    , m_heap_top(0x00020000)  // phys: zone1 above exception vectors, zone2 at 0x04000000 (above archive)
    , m_audio_open(false)
    , m_audio_device_open(false)
    , m_audio_write_count(0)
    , m_audio_device(0)
    , m_audio_mutex(nullptr)
    , m_got_call_count(0)
    , m_current_task(-1)
    , m_task_count(0)
    , m_os_ticks(0)
    , m_scheduler_started(false)
    , m_task_switched(false)
    , m_idle_pc(0)
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
    m_archive = nullptr;
    m_audio_open = false;
    m_audio_mutex = SDL_CreateMutex();
}

void Syscalls::shutdown_audio() {
    if (m_audio_device > 0) {
        SDL_CloseAudioDevice(m_audio_device);
        m_audio_device = 0;
    }
    if (m_audio_mutex) {
        SDL_LockMutex(m_audio_mutex);
        while (!m_audio_queue.empty()) m_audio_queue.pop();
        SDL_UnlockMutex(m_audio_mutex);
        SDL_DestroyMutex(m_audio_mutex);
        m_audio_mutex = nullptr;
    }
    m_audio_open = false;
    m_audio_device_open = false;
}

void SDLCALL Syscalls::audio_callback(void* userdata, Uint8* stream, int len) {
    Syscalls* sys = static_cast<Syscalls*>(userdata);
    if (!sys || !sys->m_audio_mutex) return;
    s16* buf = reinterpret_cast<s16*>(stream);
    int samples = len / 2;
    SDL_LockMutex(sys->m_audio_mutex);
    int i = 0;
    while (i < samples && !sys->m_audio_queue.empty()) {
        buf[i++] = sys->m_audio_queue.front();
        sys->m_audio_queue.pop();
    }
    while (i < samples) buf[i++] = 0;
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

const char* Syscalls::got_name(int index) const {
    static const char* names[] = {
        "abort",                 //  0: 0x80AD67E0
        "printf",                //  1: 0x80AD67E8
        "sprintf",               //  2: 0x80AD67F0
        "fprintf",               //  3: 0x80AD67F8
        "strncasecmp",           //  4: 0x80AD6800
        "malloc",                //  5: 0x80AD6808
        "realloc",               //  6: 0x80AD6810
        "free",                  //  7: 0x80AD6818
        "fread",                 //  8: 0x80AD6820
        "fwrite",                //  9: 0x80AD6828
        "fseek",                 // 10: 0x80AD6830
        "LcdGetDisMode",         // 11: 0x80AD6838
        "vxGoHome",              // 12: 0x80AD6840
        "StartSwTimer",          // 13: 0x80AD6848
        "free_irq",              // 14: 0x80AD6850
        "fsys_RefreshCache",     // 15: 0x80AD6858
        "strlen",                // 16: 0x80AD6860
        "_lcd_set_frame",        // 17: 0x80AD6868
        "_lcd_get_frame",        // 18: 0x80AD6870
        "lcd_get_cframe",        // 19: 0x80AD6878
        "ap_lcd_set_frame",      // 20: 0x80AD6880
        "lcd_flip",              // 21: 0x80AD6888
        "__icache_invalidate_all",//22: 0x80AD6890
        "__dcache_writeback_all",// 23: 0x80AD6898
        "TaskMediaFunStop",      // 24: 0x80AD68A0
        "OSCPUSaveSR",           // 25: 0x80AD68A8
        "OSCPURestoreSR",        // 26: 0x80AD68B0
        "serial_getc",           // 27: 0x80AD68B8
        "serial_putc",           // 28: 0x80AD68C0
        "_kbd_get_status",       // 29: 0x80AD68C8
        "get_game_vol",          // 30: 0x80AD68D0
        "_kbd_get_key",          // 31: 0x80AD68D8
        "fsys_fopen",            // 32: 0x80AD68E0
        "fsys_fread",            // 33: 0x80AD68E8
        "fsys_fclose",           // 34: 0x80AD68F0
        "fsys_fseek",            // 35: 0x80AD68F8
        "fsys_ftell",            // 36: 0x80AD6900
        "fsys_remove",           // 37: 0x80AD6908
        "fsys_rename",           // 38: 0x80AD6910
        "fsys_ferror",           // 39: 0x80AD6918
        "fsys_feof",             // 40: 0x80AD6920
        "fsys_fwrite",           // 41: 0x80AD6928
        "fsys_findfirst",        // 42: 0x80AD6930
        "fsys_findnext",         // 43: 0x80AD6938
        "fsys_findclose",        // 44: 0x80AD6940
        "fsys_flush_cache",      // 45: 0x80AD6948
        "USB_Connect",           // 46: 0x80AD6950
        "udc_attached",          // 47: 0x80AD6958
        "USB_No_Connect",        // 48: 0x80AD6960
        "waveout_open",          // 49: 0x80AD6968
        "waveout_close",         // 50: 0x80AD6970
        "waveout_close_at_once", // 51: 0x80AD6978
        "waveout_set_volume",    // 52: 0x80AD6980
        "HP_Mute_sw",            // 53: 0x80AD6988
        "waveout_can_write",     // 54: 0x80AD6990
        "waveout_write",         // 55: 0x80AD6998
        "pcm_can_write",         // 56: 0x80AD69A0
        "pcm_ioctl",             // 57: 0x80AD69A8
        "OSTimeGet",             // 58: 0x80AD69B0
        "OSTimeDly",             // 59: 0x80AD69B8
        "OSSemPend",             // 60: 0x80AD69C0
        "OSSemPost",             // 61: 0x80AD69C8
        "OSSemCreate",           // 62: 0x80AD69D0
        "OSTaskCreate",          // 63: 0x80AD69D8
        "OSSemDel",              // 64: 0x80AD69E0
        "OSTaskDel",             // 65: 0x80AD69E8
        "GetTickCount",          // 66: 0x80AD69F0
        "_sys_judge_event",      // 67: 0x80AD69F8
        "fsys_fopenW",           // 68: 0x80AD6A00
        "__to_unicode_le",       // 69: 0x80AD6A08
        "__to_locale_ansi",      // 70: 0x80AD6A10
        "get_current_language",  // 71: 0x80AD6A18
    };
    if (index >= 0 && index < 72) return names[index];
    return "unknown";
}

void Syscalls::dispatch(int got_index, u32 /*return_addr*/) {
    m_got_call_count++;
    if (got_index >= 0 && got_index < 72) m_got_call_counts[got_index]++;
    switch (got_index) {
    case  0: impl_abort(); break;
    case  1: impl_printf(); break;
    case  2: impl_sprintf(); break;
    case  3: impl_fprintf(); break;
    case  4: impl_strncasecmp(); break;
    case  5: impl_malloc(); break;
    case  6: impl_realloc(); break;
    case  7: impl_free(); break;
    case  8: impl_fread(); break;
    case  9: impl_fwrite(); break;
    case 10: impl_fseek(); break;
    case 11: impl_LcdGetDisMode(); break;
    case 12: impl_vxGoHome(); break;
    case 13: impl_StartSwTimer(); break;
    case 14: impl_free_irq(); break;
    case 15: impl_fsys_RefreshCache(); break;
    case 16: impl_strlen(); break;
    case 17: impl__lcd_set_frame(); break;
    case 18: impl__lcd_get_frame(); break;
    case 19: impl_lcd_get_cframe(); break;
    case 20: impl_ap_lcd_set_frame(); break;
    case 21: impl_lcd_flip(); break;
    case 22: impl___icache_invalidate_all(); break;
    case 23: impl___dcache_writeback_all(); break;
    case 24: impl_TaskMediaFunStop(); break;
    case 25: impl_OSCPUSaveSR(); break;
    case 26: impl_OSCPURestoreSR(); break;
    case 27: impl_serial_getc(); break;
    case 28: impl_serial_putc(); break;
    case 29: impl__kbd_get_status(); break;
    case 30: impl_get_game_vol(); break;
    case 31: impl__kbd_get_key(); break;
    case 32: impl_fsys_fopen(); break;
    case 33: impl_fsys_fread(); break;
    case 34: impl_fsys_fclose(); break;
    case 35: impl_fsys_fseek(); break;
    case 36: impl_fsys_ftell(); break;
    case 37: impl_fsys_remove(); break;
    case 38: impl_fsys_rename(); break;
    case 39: impl_fsys_ferror(); break;
    case 40: impl_fsys_feof(); break;
    case 41: impl_fsys_fwrite(); break;
    case 42: impl_fsys_findfirst(); break;
    case 43: impl_fsys_findnext(); break;
    case 44: impl_fsys_findclose(); break;
    case 45: impl_fsys_flush_cache(); break;
    case 46: impl_USB_Connect(); break;
    case 47: impl_udc_attached(); break;
    case 48: impl_USB_No_Connect(); break;
    case 49: impl_waveout_open(); break;
    case 50: impl_waveout_close(); break;
    case 51: impl_waveout_close_at_once(); break;
    case 52: impl_waveout_set_volume(); break;
    case 53: impl_HP_Mute_sw(); break;
    case 54: impl_waveout_can_write(); break;
    case 55: impl_waveout_write(); break;
    case 56: impl_pcm_can_write(); break;
    case 57: impl_pcm_ioctl(); break;
    case 58: impl_OSTimeGet(); break;
    case 59: impl_OSTimeDly(); break;
    case 60: impl_OSSemPend(); break;
    case 61: impl_OSSemPost(); break;
    case 62: impl_OSSemCreate(); break;
    case 63: impl_OSTaskCreate(); break;
    case 64: impl_OSSemDel(); break;
    case 65: impl_OSTaskDel(); break;
    case 66: impl_GetTickCount(); break;
    case 67: impl__sys_judge_event(); break;
    case 68: impl_fsys_fopenW(); break;
    case 69: impl___to_unicode_le(); break;
    case 70: impl___to_locale_ansi(); break;
    case 71: impl_get_current_language(); break;
    default:
        printf("[SYSCALL] Unknown GOT index %d\n", got_index);
        break;
    }
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
    int idx = file_handle;
    if (idx < 0 || idx >= 64 || !m_files[idx].in_use) return 0;
    u32 total = size * nmemb;
    if (m_files[idx].is_host && m_files[idx].host_file) {
        std::vector<u8> buf(total);
        size_t read = fread(buf.data(), 1, total, m_files[idx].host_file);
        m_mem.write_block(ptr, buf.data(), (u32)read);
        m_files[idx].offset += (u32)read;
        return (u32)(read / size);
    } else if (m_files[idx].is_archive && m_files[idx].archive_entry) {
        u32 available = m_files[idx].archive_entry->size - m_files[idx].offset;
        u32 to_read = std::min(total, available);
        if (to_read > 0) {
            std::vector<u8> buf(to_read);
            m_files[idx].archive->read(*m_files[idx].archive_entry, buf.data(), m_files[idx].offset, to_read);
            m_mem.write_block(ptr, buf.data(), to_read);
            m_files[idx].offset += to_read;
        }
        return to_read / size;
    } else {
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
    int idx = file_handle;
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
    int idx = file_handle;
    if (idx < 0 || idx >= 64 || !m_files[idx].in_use) return (u32)-1;
    if (m_files[idx].is_host && m_files[idx].host_file) {
        u32 ret = (u32)fseek(m_files[idx].host_file, offset, (int)whence);
        if (ret == 0) m_files[idx].offset = (u32)ftell(m_files[idx].host_file);
        return ret;
    } else if (m_files[idx].is_archive && m_files[idx].archive_entry) {
        u32 file_size = m_files[idx].archive_entry->size;
        if (whence == 0) m_files[idx].offset = (u32)offset;
        else if (whence == 1) m_files[idx].offset += (u32)offset;
        else if (whence == 2) m_files[idx].offset = file_size + (u32)offset;
        if (m_files[idx].offset > file_size) m_files[idx].offset = file_size;
        return 0;
    } else {
        if (whence == 0) m_files[idx].offset = (u32)offset;
        else if (whence == 1) m_files[idx].offset += (u32)offset;
        else if (whence == 2) m_files[idx].offset = (u32)m_files[idx].embedded_data.size() + (u32)offset;
        return 0;
    }
}

u32 Syscalls::do_ftell(u32 file_handle) {
    int idx = file_handle;
    if (idx < 0 || idx >= 64 || !m_files[idx].in_use) return (u32)-1;
    if (m_files[idx].is_host && m_files[idx].host_file)
        return (u32)ftell(m_files[idx].host_file);
    return m_files[idx].offset;
}

u32 Syscalls::do_feof(u32 file_handle) {
    int idx = file_handle;
    if (idx < 0 || idx >= 64 || !m_files[idx].in_use) return 1;
    if (m_files[idx].is_host && m_files[idx].host_file)
        return (u32)feof(m_files[idx].host_file);
    if (m_files[idx].is_archive && m_files[idx].archive_entry)
        return m_files[idx].offset >= m_files[idx].archive_entry->size ? 1 : 0;
    return m_files[idx].offset >= m_files[idx].embedded_data.size() ? 1 : 0;
}

u32 Syscalls::do_ferror(u32 file_handle) {
    int idx = file_handle;
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
    g_cpu_regs[2] = m_display.is_display_on() ? 1 : 0;
}

void Syscalls::impl_vxGoHome() {
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_StartSwTimer() {
    printf("[TIMER] StartSwTimer(%u, 0x%08X)\n", arg(0), arg(1));
    g_cpu_regs[2] = 1;
}

void Syscalls::impl_free_irq() {
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_fsys_RefreshCache() {
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_strlen() {
    g_cpu_regs[2] = (u32)guest_string(arg(0)).size();
}

void Syscalls::impl__lcd_set_frame() {
    static constexpr u32 FB_SIZE = Display::WIDTH * Display::HEIGHT * 2;
    static u32 s_call_count = 0;
    s_call_count++;

    u32 end_ptr = arg(0) & 0x1FFFFFFF;
    if (end_ptr >= FB_SIZE && end_ptr < m_mem.size()) {
        u32 actual_start = end_ptr - FB_SIZE;
        u32 old_front = m_display.get_frame_addr();
        if (!old_front && g_detected_fb_addr)
            old_front = g_detected_fb_addr + FB_SIZE;

        if (s_call_count <= 5 || s_call_count % 100 == 0) {
            const u8* ram = m_mem.get_raw_ptr();
            u32 phys = actual_start & 0x1FFFFFFF;
            if (ram && phys + FB_SIZE <= m_mem.size()) {
                const u16* px = reinterpret_cast<const u16*>(&ram[phys]);
                u32 nonzero = 0;
                for (u32 i = 0; i < Display::WIDTH * Display::HEIGHT; i++)
                    if (px[i]) nonzero++;
                printf("[LCD] flip #%u phys=0x%08X nonzero=%u\n",
                       s_call_count, actual_start, nonzero);
            }
        }

        m_display.set_frame_addr(actual_start);
        m_display.set_back_addr(old_front);
    } else {
        if (s_call_count <= 5)
            printf("[LCD] skip flip #%u end_ptr=0x%08X (bad range)\n", s_call_count, end_ptr);
    }

    // Dynamically detect CG buffer (8-bit indexed at ~phys 0x100000)
    {
        static bool cg_set = false;
        if (!cg_set) {
            const u8* ram = m_mem.get_raw_ptr();
            u32 cg_phys = 0;
            int best_score = 0;
            u32 fb_phys = m_display.get_frame_addr() & 0x1FFFFFFF;
            if (!fb_phys) fb_phys = g_detected_fb_addr;
            u32 scan_start = 0x0F0000;
            u32 scan_end = (fb_phys >= 0x120000) ? 0x120000 : (fb_phys > 0x10000 ? fb_phys - 0x10000 : 0);
            for (u32 base = scan_start; base < scan_end; base += 0x10000) {
                if (base + 153600 > m_mem.size()) break;
                u8 hb_seen[256] = {0};
                u32 total_nz = 0, hb_count = 0;
                for (int i = 0; i < 153600 && base + i + 1 <= m_mem.size(); i += 2) {
                    if (ram[base + i] | ram[base + i + 1]) {
                        total_nz++;
                        if (!hb_seen[ram[base + i + 1]]) {
                            hb_seen[ram[base + i + 1]] = 1;
                            hb_count++;
                        }
                    }
                }
                int score = (int)(total_nz / 768) + (int)(hb_count * 100);
                if (score > best_score) {
                    best_score = score;
                    cg_phys = base;
                }
            }
            if (cg_phys && best_score > 300) {
                m_display.set_cg_addr(cg_phys);
                printf("[CG] Compositing enabled: CG at phys 0x%08X (score=%d)\n", cg_phys, best_score);

                // Search for an embedded palette in the game code+data section
                bool pal_found = false;
                const u8* ram = m_mem.get_raw_ptr();
                for (u32 base = 0xA00000; base < 0xB50000 && !pal_found; base += 4) {
                    if (base + 512 > m_mem.size()) break;
                    const u16* pal = reinterpret_cast<const u16*>(&ram[base]);
                    if (pal[0] != 0) continue;
                    // Need non-zero entries in first 16 AND between 128-255
                    bool has_lo = false, has_hi = false;
                    for (int i = 1; i < 16; i++) if (pal[i]) { has_lo = true; break; }
                    for (int i = 128; i < 256; i++) if (pal[i]) { has_hi = true; break; }
                    if (!has_lo || !has_hi) continue;
                    // Strict gradient: strictly increasing for most entries
                    int increasing = 0;
                    for (int i = 1; i < 256; i++)
                        if (pal[i] > pal[i-1]) increasing++;
                    if (increasing >= 200) {
                        printf("[CG] Found palette at phys 0x%08X: [%04X %04X %04X %04X ... %04X]\n",
                               base, pal[0], pal[1], pal[2], pal[3], pal[255]);
                        memcpy(m_mem.get_lcd_palette(), pal, 512);
                        pal_found = true;
                    }
                }

                if (!pal_found) {
                    u16* pal = m_mem.get_lcd_palette();
                    for (int i = 0; i < 256; i++) {
                        u8 v = (u8)i;
                        pal[i] = ((u16)(v >> 3) << 11) | ((u16)(v >> 2) << 5) | (v >> 3);
                    }
                    printf("[CG] Using grayscale fallback palette\n");
                }
            }
            cg_set = true;
        }
    }
    m_display.set_lcd_palette(m_mem.get_lcd_palette());
    m_display.flip(m_mem.get_raw_ptr(), m_mem.size());
    g_cpu_regs[2] = 0;
}

void Syscalls::impl__lcd_get_frame() {
    u32 phys = m_display.get_back_addr();
    if (!phys) phys = g_detected_fb_addr;
    g_cpu_regs[2] = phys ? (phys | 0xa0000000u) : 0;
}

void Syscalls::impl_lcd_get_cframe() {
    // Real OS: lcd_get_cframe() = jzfb.cframe = front buffer | 0x80000000 (KSEG0, cached).
    u32 phys = m_display.get_frame_addr();
    if (!phys) phys = g_detected_fb_addr;
    g_cpu_regs[2] = phys ? (phys | 0x80000000u) : 0;
}

void Syscalls::impl_ap_lcd_set_frame() {
    m_display.set_frame_addr(arg(0) & 0x1FFFFFFF);
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_lcd_flip() {
    // Real OS: lcd_change_frame() — advance the DMA descriptor ring and present.
    // The game's framebuffer is RGB565 (16bpp): use flip() not flip_argb8888().
    m_display.flip(m_mem.get_raw_ptr(), m_mem.size());
    g_cpu_regs[2] = 0;
}

void Syscalls::impl___icache_invalidate_all() {
    g_cpu_regs[2] = 0;
}

void Syscalls::impl___dcache_writeback_all() {
    g_cpu_regs[2] = 0;
}

// === GOT 24-31: media / OS / serial / input ===

void Syscalls::impl_TaskMediaFunStop() {
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_OSCPUSaveSR() {
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_OSCPURestoreSR() {
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_serial_getc() {
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_serial_putc() {
    putchar((char)arg(0));
    fflush(stdout);
    g_cpu_regs[2] = 0;
}

void Syscalls::impl__kbd_get_status() {
    u32 keys = m_display.get_dingoo_keys();
    g_cpu_regs[2] = keys;

    // The real Dingoo OS _kbd_get_status also writes the key state into a
    // memory-mapped OS buffer at 0x80B49D08. The game's event dispatcher
    // (at 0x80A000FC) reads that address immediately after calling this
    // function and dispatches per-key events. Specifically:
    //   andi $v0, $v0, 0x0040  → DKEY_A  → event code 2 (advance text / confirm)
    //   andi $v0, $v0, 0x0800  → DKEY_R  → event code 2 (same)
    // Without this write, 0x80B49D08 is always 0 and the game never receives
    // any key events — dialogue is permanently frozen.
    m_mem.write_u32(0x80B49D08, keys);
}

void Syscalls::impl_get_game_vol() {
    g_cpu_regs[2] = 80;
}

void Syscalls::impl__kbd_get_key() {
    u32 keys = m_display.get_dingoo_keys();
    if (keys & DKEY_UP)       { g_cpu_regs[2] = DKEY_UP; return; }
    if (keys & DKEY_DOWN)     { g_cpu_regs[2] = DKEY_DOWN; return; }
    if (keys & DKEY_LEFT)     { g_cpu_regs[2] = DKEY_LEFT; return; }
    if (keys & DKEY_RIGHT)    { g_cpu_regs[2] = DKEY_RIGHT; return; }
    if (keys & DKEY_A)        { g_cpu_regs[2] = DKEY_A; return; }
    if (keys & DKEY_B)        { g_cpu_regs[2] = DKEY_B; return; }
    if (keys & DKEY_X)        { g_cpu_regs[2] = DKEY_X; return; }
    if (keys & DKEY_Y)        { g_cpu_regs[2] = DKEY_Y; return; }
    if (keys & DKEY_L)        { g_cpu_regs[2] = DKEY_L; return; }
    if (keys & DKEY_R)        { g_cpu_regs[2] = DKEY_R; return; }
    if (keys & DKEY_START)    { g_cpu_regs[2] = DKEY_START; return; }
    if (keys & DKEY_SELECT)   { g_cpu_regs[2] = DKEY_SELECT; return; }
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
            g_cpu_regs[2] = (u32)idx;
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
            g_cpu_regs[2] = (u32)idx;
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
    u32 before = (handle < 64 && m_files[handle].in_use) ? m_files[handle].offset : 0;
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
    printf("[FCLOSE] handle=%u\n", arg(0));
    close_file_handle(arg(0));
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
    printf("[FSYS] remove() - stub\n");
    g_cpu_regs[2] = (u32)-1;
}

void Syscalls::impl_fsys_rename() {
    printf("[FSYS] rename() - stub\n");
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

void Syscalls::impl_fsys_findfirst() {
    printf("[FSYS] findfirst() - stub\n");
    g_cpu_regs[2] = (u32)-1;
}

void Syscalls::impl_fsys_findnext() {
    printf("[FSYS] findnext() - stub\n");
    g_cpu_regs[2] = (u32)-1;
}

void Syscalls::impl_fsys_findclose() {
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_fsys_flush_cache() {
    g_cpu_regs[2] = 0;
}

// === GOT 46-48: USB ===

void Syscalls::impl_USB_Connect() {
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_udc_attached() {
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_USB_No_Connect() {
    g_cpu_regs[2] = 0;
}

// === GOT 49-57: audio ===

void Syscalls::impl_waveout_open() {
    int sample_rate = (int)arg(0);
    int channels = (int)arg(1);
    int bits = (int)arg(2);

    if (sample_rate <= 0) sample_rate = 44100;
    if (channels <= 0) channels = 2;
    if (bits <= 0) bits = 16;

    if (m_audio_device > 0) {
        SDL_CloseAudioDevice(m_audio_device);
        m_audio_device = 0;
    }

    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = sample_rate;
    want.format = AUDIO_S16SYS;
    want.channels = (Uint8)channels;
    want.samples = 2048;
    want.callback = audio_callback;
    want.userdata = this;

    m_audio_device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (m_audio_device > 0) {
        SDL_PauseAudioDevice(m_audio_device, 0);
        m_audio_open = true;
        m_audio_device_open = true;
        printf("[AUDIO] waveout_open: %dHz %dch %dbit -> device=%u (got %dHz %dch)\n",
               sample_rate, channels, bits, m_audio_device, have.freq, have.channels);
        g_cpu_regs[2] = 0;
    } else {
        printf("[AUDIO] waveout_open FAILED: %s\n", SDL_GetError());
        m_audio_open = false;
        m_audio_device_open = false;
        g_cpu_regs[2] = (u32)-1;
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
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_HP_Mute_sw() {
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_waveout_write() {
    u32 buf_addr = arg(0);
    u32 size = arg(1);

    if (size == 0 || !buf_addr) {
        g_cpu_regs[2] = 0;
        return;
    }

    u32 count = size / 2;
    std::vector<s16> samples(count);
    m_mem.read_block(buf_addr, (u8*)samples.data(), size);

    SDL_LockMutex(m_audio_mutex);
    for (u32 i = 0; i < count; i++)
        m_audio_queue.push(samples[i]);
    SDL_UnlockMutex(m_audio_mutex);

    g_cpu_regs[2] = size;
    m_audio_write_count++;
}

void Syscalls::impl_waveout_can_write() {
    // Return available write space in bytes (max 64KB ring, subtract queued)
    SDL_LockMutex(m_audio_mutex);
    size_t queued = m_audio_queue.size();
    SDL_UnlockMutex(m_audio_mutex);
    u32 free = (queued >= 32768) ? 0 : (u32)((32768 - queued) * 2);
    g_cpu_regs[2] = free ? free : 4096;
}

void Syscalls::impl_pcm_can_write() {
    impl_waveout_can_write();
}

void Syscalls::impl_pcm_ioctl() {
    g_cpu_regs[2] = 0;
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
    // Find a task that is active and not blocked
    for (int i = 0; i < m_task_count; i++) {
        if (m_tasks[i].active && !m_tasks[i].blocked)
            return i;
    }
    return -1;
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

    // Real µC/OS-II rejects duplicate priorities with OS_PRIO_EXIST=40.
    // This emulator historically allowed them (round-robin); keep that for
    // compatibility but warn loudly so we notice if the guest hits it.
    for (int i = 0; i < m_task_count; i++) {
        if (m_tasks[i].active && m_tasks[i].task_prio == (u8)prio) {
            printf("[OSTaskCreate] WARNING: duplicate priority %u for task %d (allowing)\n", (u8)prio, i);
            break;
        }
    }

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

        m_task_count++;
        printf("[OSTaskCreate] Created task %d: entry=0x%08X prio=%u\n",
               m_task_count - 1, entry, (u8)prio);
        g_cpu_regs[2] = 0;
    } else {
        printf("[OSTaskCreate] FAILED: max tasks (%d) reached\n", MAX_TASKS);
        g_cpu_regs[2] = 0xFF;
    }

    // Don't auto-start the scheduler here.
    // The init code (GameEngineInit) needs to finish setting up before tasks run.
    // On real Dingoo µC/OS-II, this would return to the caller which then calls OSStart.
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

    save_current_task();
    m_tasks[m_current_task].blocked = true;
    m_tasks[m_current_task].wake_tick = m_os_ticks + ticks;

    int next = find_ready_task();
    if (next >= 0) {
        switch_to_task(next);
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
        printf("[OSTaskDel] Deleted self (task %d), resumed idle\n", deleted_task);
        memcpy(g_cpu_regs, m_idle_regs, sizeof(g_cpu_regs));
        g_cpu_pc = m_idle_pc;
        m_task_switched = true;
    }
}

void Syscalls::impl_GetTickCount() {
    g_cpu_regs[2] = (u32)(SDL_GetTicks());
}

void Syscalls::impl__sys_judge_event() {
    u32 event_queue = 0x80BFECD8;
    u32 event_val = m_mem.read_u32(event_queue);
    if (event_val) {
        m_mem.write_u32(event_queue, 0);
        g_cpu_regs[2] = event_val;
    } else {
        g_cpu_regs[2] = 0;
    }
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

    // If path is "7days" (no extension), the game is opening its own binary to read
    // embedded data (resource offsets, etc.).  Redirect to the actual .app file.
    if (!m_app_path.empty() && (path == "7days" || path == "7days.app")) {
        int idx = alloc_file_handle();
        if (idx < 0) { g_cpu_regs[2] = 0; return; }
        FILE* f = fopen(m_app_path.c_str(), "rb");
        if (f) {
            m_files[idx].is_host = true;
            m_files[idx].host_file = f;
            printf("[fopenW] '%s' mode='%s' -> handle %d (app binary)\n",
                   path.c_str(), mode.c_str(), idx);
            g_cpu_regs[2] = (u32)idx;
            return;
        }
        m_files[idx].in_use = false;
    }

    // If path is garbage, try game init files in order
    std::string search_path = path;
    if (search_path.empty() || search_path.find_first_not_of(' ') == std::string::npos ||
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
    }

    // Look up in archive first
    if (m_archive) {
        const ArchiveEntry* entry = m_archive->find(search_path);
        if (entry) {
            int idx = alloc_file_handle();
            if (idx < 0) { g_cpu_regs[2] = 0; return; }
            m_files[idx].is_archive = true;
            m_files[idx].archive = m_archive;
            m_files[idx].archive_entry = entry;
            m_files[idx].offset = 0;
            printf("[fopenW] '%s' mode='%s' -> handle %d (archive, %u bytes)\n",
                   search_path.c_str(), mode.c_str(), idx, (u32)entry->size);
            g_cpu_regs[2] = (u32)idx;
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
                   path.c_str(), mode.c_str(), idx);
            g_cpu_regs[2] = (u32)idx;
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
                   path.c_str(), mode.c_str(), idx);
            g_cpu_regs[2] = (u32)idx;
            return;
        }
    }

    printf("[fopenW] '%s' mode='%s' -> NOT FOUND\n", search_path.c_str(), mode.c_str());
    m_files[idx].in_use = false;
    g_cpu_regs[2] = 0;
}

void Syscalls::impl___to_unicode_le() {
    g_cpu_regs[2] = (u32)-1;
}

void Syscalls::impl___to_locale_ansi() {
    g_cpu_regs[2] = (u32)-1;
}

void Syscalls::impl_get_current_language() {
    g_cpu_regs[2] = 1; // Chinese (0=English)
}

// === VSYNC simulation ===

bool Syscalls::simulate_vsync() {
    bool switched = false;

    // Auto-start the scheduler if OSStart has already run (task code is executing)
    if (m_current_task < 0 && m_task_count > 0) {
        int highest = -1;
        u8 best_prio = 255;
        for (int i = 0; i < m_task_count; i++) {
            if (m_tasks[i].active && !m_tasks[i].blocked && m_tasks[i].task_prio < best_prio) {
                highest = i;
                best_prio = m_tasks[i].task_prio;
            }
        }
        if (highest >= 0) {
            memcpy(m_tasks[highest].regs, g_cpu_regs, sizeof(g_cpu_regs));
            m_tasks[highest].hi = g_cpu_hi;
            m_tasks[highest].lo = g_cpu_lo;
            m_current_task = highest;
            printf("[SCHEDULER] Auto-start: task %d (prio %u) running\n", highest, best_prio);
        }
    }

    // Advance µC/OS-II tick counter (approx 1 tick per frame = 16.6ms)
    m_os_ticks += 1;

    // Wake tasks whose OSTimeDly or OSSemPend timeout has expired.
    for (int i = 0; i < m_task_count; i++) {
        if (m_tasks[i].blocked && m_tasks[i].wake_tick > 0 && m_os_ticks >= m_tasks[i].wake_tick) {
            if (m_tasks[i].block_sem) {
                // OSSemPend timeout: signal OS_TIMEOUT to the caller.
                if (m_tasks[i].sem_err_ptr)
                    m_mem.write_u8(m_tasks[i].sem_err_ptr, 10);  // OS_TIMEOUT
                m_tasks[i].sem_err_ptr = 0;
                m_tasks[i].block_sem = 0;
            }
            m_tasks[i].blocked = false;
            m_tasks[i].wake_tick = 0;
        }
    }

    // Detect key events and inject into the event queue
    static u32 vsync_count = 0;
    vsync_count++;

    // Check for key events
    u32 keys = m_display.get_dingoo_keys();
    static u32 prev_keys = 0;
    u32 pressed = keys & ~prev_keys;
    prev_keys = keys;

    // Auto-press sequence:
    //   vsync 1000      : START (dismiss "press start" / title logo screen)
    //   vsync 1100      : A    (select first menu item = "New Game")
    //   vsync 1200+     : A every 20 vsyncs to advance story dialogue
    //   (hold 5 vsyncs each press so _kbd_get_status sees the transition)
    static u32 auto_release_frame = 0;
    static u32 auto_held = 0;
    // Release previous key
    if (auto_held && vsync_count == auto_release_frame) {
        m_display.set_key(auto_held, false);
        auto_held = 0;
    }
    // Determine which key (if any) to press this vsync
    u32 press_key = 0;
    if (vsync_count == 100) press_key = DKEY_START;
    else if (vsync_count >= 150 && vsync_count % 10 == 0) press_key = DKEY_A;

    if (press_key && !auto_held) {
        m_display.set_key(press_key, true);
        auto_held = press_key;
        // Hold for 5 vsyncs so that _kbd_get_status (polled ~1/5 vsyncs) sees it
        auto_release_frame = vsync_count + 5;
    }

    // Write pressed keys to the event queue for game code to read via _sys_judge_event
    if (pressed) {
        u32 event_queue = 0x80BFECD8;
        m_mem.write_u32(event_queue, pressed);
    }

    // Cooperative multitasking: if current task is not blocked, yield to others
    if (m_current_task >= 0 && m_current_task < m_task_count) {
        Task& current = m_tasks[m_current_task];
        if (current.blocked) {
            // Current task is blocked (on semaphore or delay) — try to find a ready task
            save_current_task();
            int next = find_ready_task();
            if (next >= 0) {
                switch_to_task(next);
                switched = true;
            }
        } else {
            // Current task is still running — preempt and yield to another ready task
            for (int i = 0; i < m_task_count; i++) {
                if (i != m_current_task && m_tasks[i].active && !m_tasks[i].blocked) {
                    save_current_task();  // saves g_cpu_pc into tasks[current].pc (no $ra corruption)
                    switch_to_task(i);
                    switched = true;
                    break;
                }
            }
        }
    } else {
        int next = find_ready_task();
        if (next >= 0) {
            switch_to_task(next);
            switched = true;
        }
    }

    // Keep SDL window alive without triggering frame-count dirty flag.
    m_display.present_blank();

    // Periodic GOT call dump every 1000 frames
    if (vsync_count % 1000 == 0) {
        for (int i = 0; i < 72; i++) {
            if (m_got_call_counts[i] > 0) {
                printf("[GOT] %3d: %-25s %u\n", i, got_name(i), m_got_call_counts[i]);
            }
        }
    }
    return switched;
}
