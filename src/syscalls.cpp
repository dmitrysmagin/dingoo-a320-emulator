#include "syscalls.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>

// We need access to CPU registers for syscall dispatch
// These are set by the CPU before calling dispatch()
extern u32 g_cpu_regs[32];
extern u32 g_cpu_pc;

Syscalls::Syscalls(Memory& mem, Display& display)
    : m_mem(mem)
    , m_display(display)
    , m_heap_top(0x00B45000)  // start of heap area
    , m_got_call_count(0)
{
    for (int i = 0; i < 64; i++) {
        m_files[i].in_use = false;
        m_files[i].is_host = false;
        m_files[i].host_file = nullptr;
        m_files[i].offset = 0;
    }
}

u32 Syscalls::arg(int n) {
    if (n >= 0 && n <= 3) return g_cpu_regs[4 + n];  // $a0..$a3
    // From stack: $sp + 16 + n*4
    u32 sp = g_cpu_regs[29];
    return m_mem.read_u32(sp + 16 + (u32)n * 4);
}

std::string Syscalls::guest_string(u32 vaddr) {
    return m_mem.read_string(vaddr);
}

const char* Syscalls::got_name(int index) const {
    static const char* names[] = {
        "abort", "printf", "sprintf", "fprintf", "strncasecmp",
        "malloc", "realloc", "free", "fread", "fwrite",
        "fopen", "fclose", "fseek", "ftell", "fgets",
        "fflush", "feof", "ferror", "fgetc", "fputc",
        "setbuf", "setvbuf", "exit", "atexit", "getenv",
        "strncpy", "strncmp", "strcpy", "strcmp", "strlen",
        "memset", "memcpy", "memmove", "memcmp", "strstr",
        "strcat", "strchr", "strrchr", "strtok", "sscanf",
        "rand", "srand", "qsort", "bsearch", "abs",
        "atoi", "atof", "strtol", "strtoul", "strtod",
        "fsys_fopen", "fsys_fread", "fsys_fseek", "fsys_fclose", "fsys_ftell",
        "fsys_fgets", "fsys_feof", "fsys_ferror", "fsys_fgetc", "fsys_fputc",
        "lcd_flip", "_lcd_set_frame", "LcdGetDisMode", "_kbd_get_key", "_kbd_get_status",
        "waveout_open", "waveout_write", "waveout_close", "OSTimeGet", "OSTimeDly",
        "OSSemCreate", "OSSemPend", "OSSemPost", "OSSemDel", "OSTaskCreate",
        "StartSwTimer", "free_irq", "__icache_invalidate_all", "__dcache_writeback_all",
        "serial_putc", "serial_getc",
    };
    if (index >= 0 && index < 72) return names[index];
    return "unknown";
}

void Syscalls::dispatch(int got_index, u32 return_addr) {
    m_got_call_count++;
    switch (got_index) {
    case 0:  impl_abort(); break;
    case 1:  impl_printf(); break;
    case 2:  impl_sprintf(); break;
    case 3:  impl_fprintf(); break;
    case 4:  impl_strncasecmp(); break;
    case 5:  impl_malloc(); break;
    case 6:  impl_realloc(); break;
    case 7:  impl_free(); break;
    case 8:  impl_fread(); break;
    case 9:  impl_fwrite(); break;
    case 10: impl_fopen(); break;
    case 11: impl_fclose(); break;
    case 12: impl_fseek(); break;
    case 13: impl_ftell(); break;
    case 14: impl_fgets(); break;
    case 15: impl_fflush(); break;
    case 16: impl_feof(); break;
    case 17: impl_ferror(); break;
    case 18: impl_fgetc(); break;
    case 19: impl_fputc(); break;
    case 20: impl_setbuf(); break;
    case 21: impl_setvbuf(); break;
    case 22: impl_exit(); break;
    case 23: impl_atexit(); break;
    case 24: impl_getenv(); break;
    case 25: impl_strncpy(); break;
    case 26: impl_strncmp(); break;
    case 27: impl_strcpy(); break;
    case 28: impl_strcmp(); break;
    case 29: impl_strlen(); break;
    case 30: impl_memset(); break;
    case 31: impl_memcpy(); break;
    case 32: impl_memmove(); break;
    case 33: impl_memcmp(); break;
    case 34: impl_strstr(); break;
    case 35: impl_strcat(); break;
    case 36: impl_strchr(); break;
    case 37: impl_strrchr(); break;
    case 38: impl_strtok(); break;
    case 39: impl_sscanf(); break;
    case 40: impl_rand(); break;
    case 41: impl_srand(); break;
    case 42: impl_qsort(); break;
    case 43: impl_bsearch(); break;
    case 44: impl_abs(); break;
    case 45: impl_atoi(); break;
    case 46: impl_atof(); break;
    case 47: impl_strtol(); break;
    case 48: impl_strtoul(); break;
    case 49: impl_strtod(); break;
    case 50: impl_fsys_fopen(); break;
    case 51: impl_fsys_fread(); break;
    case 52: impl_fsys_fseek(); break;
    case 53: impl_fsys_fclose(); break;
    case 54: impl_fsys_ftell(); break;
    case 55: impl_fsys_fgets(); break;
    case 56: impl_fsys_feof(); break;
    case 57: impl_fsys_ferror(); break;
    case 58: impl_fsys_fgetc(); break;
    case 59: impl_fsys_fputc(); break;
    case 60: impl_lcd_flip(); break;
    case 61: impl_lcd_set_frame(); break;
    case 62: impl_LcdGetDisMode(); break;
    case 63: impl_kbd_get_key(); break;
    case 64: impl_kbd_get_status(); break;
    case 65: impl_waveout_open(); break;
    case 66: impl_waveout_write(); break;
    case 67: impl_waveout_close(); break;
    case 68: impl_OSTimeGet(); break;
    case 69: impl_OSTimeDly(); break;
    case 70: impl_OSSemCreate(); break;
    case 71: impl_OSSemPend(); break;
    default:
        // These are beyond 72, but let's handle them too
        if (got_index == 72) { impl_OSSemPost(); break; }
        if (got_index == 73) { impl_OSSemDel(); break; }
        if (got_index == 74) { impl_OSTaskCreate(); break; }
        if (got_index == 75) { impl_StartSwTimer(); break; }
        if (got_index == 76) { impl_free_irq(); break; }
        if (got_index == 77) { impl_icache_invalidate_all(); break; }
        if (got_index == 78) { impl_dcache_writeback_all(); break; }
        if (got_index == 79) { impl_serial_putc(); break; }
        if (got_index == 80) { impl_serial_getc(); break; }
        printf("[SYSCALL] Unknown GOT index %d\n", got_index);
        break;
    }

    // Set $ra = return_addr, $v0 = result (already set by impl)
    g_cpu_regs[31] = return_addr;
}

// === Heap management ===

u32 Syscalls::heap_alloc(u32 size) {
    if (size == 0) size = 1;
    // Align to 8 bytes
    size = (size + 7) & ~7;

    // Find a free block
    for (auto& block : m_heap) {
        if (block.free && block.size >= size) {
            block.free = false;
            printf("[HEAP] alloc(%u) -> 0x%08X\n", size, block.addr);
            return block.addr;
        }
    }

    // Allocate new block
    u32 addr = m_heap_top;
    m_heap_top += size;
    if (m_heap_top > 0x01000000) {
        printf("[HEAP] WARNING: heap exceeded 16MB boundary\n");
        return 0;
    }
    m_heap.push_back({addr, size, false});
    printf("[HEAP] alloc(%u) -> 0x%08X (new)\n", size, addr);
    return addr;
}

void Syscalls::heap_free(u32 addr) {
    if (addr == 0) return;
    for (auto& block : m_heap) {
        if (block.addr == addr && !block.free) {
            block.free = true;
            printf("[HEAP] free(0x%08X)\n", addr);
            return;
        }
    }
    printf("[HEAP] free(0x%08X) - not found\n", addr);
}

u32 Syscalls::heap_realloc(u32 addr, u32 new_size) {
    if (addr == 0) return heap_alloc(new_size);
    if (new_size == 0) { heap_free(addr); return 0; }

    for (auto& block : m_heap) {
        if (block.addr == addr && !block.free) {
            if (new_size <= block.size) return addr;  // fits
            // Need to reallocate
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

// === File handle management ===

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
    m_files[idx].embedded_data.clear();
}

// === libc / memory implementations ===

void Syscalls::impl_abort() {
    printf("[SYSCALL] abort()\n");
    g_cpu_regs[2] = 0;  // $v0
}

void Syscalls::impl_printf() {
    u32 fmt_addr = arg(0);
    std::string fmt = guest_string(fmt_addr);

    // Simple printf: just log the format string and args
    // For Phase 1, we don't fully parse format strings
    printf("[PRINTF] ");

    // Try to print the first few args based on format specifiers
    int arg_idx = 1;
    for (size_t i = 0; i < fmt.size(); i++) {
        if (fmt[i] == '%' && i + 1 < fmt.size()) {
            char spec = fmt[i + 1];
            if (spec == 's') {
                u32 saddr = arg(arg_idx++);
                printf("%s", guest_string(saddr).c_str());
            } else if (spec == 'd' || spec == 'i') {
                printf("%d", (s32)arg(arg_idx++));
            } else if (spec == 'u') {
                printf("%u", arg(arg_idx++));
            } else if (spec == 'x' || spec == 'X') {
                printf("%08X", arg(arg_idx++));
            } else if (spec == 'p') {
                printf("%08X", arg(arg_idx++));
            } else if (spec == 'f' || spec == 'g') {
                // Float args are passed differently on MIPS (FPU regs)
                // For now, skip
                printf("<float>");
                arg_idx++;
            } else if (spec == 'c') {
                printf("%c", (char)arg(arg_idx++));
            } else if (spec == '%') {
                putchar('%');
            } else {
                putchar('%');
                putchar(spec);
            }
            i++;  // skip specifier
        } else {
            putchar(fmt[i]);
        }
    }
    putchar('\n');
    fflush(stdout);

    g_cpu_regs[2] = 0;  // return value (chars printed, approximate)
}

void Syscalls::impl_sprintf() {
    u32 buf_addr = arg(0);
    u32 fmt_addr = arg(1);
    std::string fmt = guest_string(fmt_addr);

    // Simplified: just write the format string to buffer
    // Real sprintf would parse format specifiers
    std::string result = fmt;  // Phase 1: just copy format string
    for (u32 i = 0; i < result.size() && i < 255; i++) {
        m_mem.write_u8(buf_addr + i, (u8)result[i]);
    }
    m_mem.write_u8(buf_addr + result.size(), 0);

    g_cpu_regs[2] = (u32)result.size();
}

void Syscalls::impl_fprintf() {
    u32 file = arg(0);
    u32 fmt_addr = arg(1);
    std::string fmt = guest_string(fmt_addr);
    printf("[FPRINTF] file=%d fmt=\"%s\"\n", file, fmt.c_str());
    g_cpu_regs[2] = 0;
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
    if (result == 0 && a.size() != b.size()) {
        result = (a.size() < b.size()) ? -1 : 1;
    }

    g_cpu_regs[2] = (u32)result;
}

void Syscalls::impl_malloc() {
    u32 size = arg(0);
    u32 addr = heap_alloc(size);
    g_cpu_regs[2] = addr;
}

void Syscalls::impl_realloc() {
    u32 addr = arg(0);
    u32 size = arg(1);
    g_cpu_regs[2] = heap_realloc(addr, size);
}

void Syscalls::impl_free() {
    u32 addr = arg(0);
    heap_free(addr);
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_fread() {
    u32 ptr = arg(0);
    u32 size = arg(1);
    u32 nmemb = arg(2);
    u32 file_handle = arg(3);

    int idx = file_handle;
    if (idx < 0 || idx >= 64 || !m_files[idx].in_use) {
        g_cpu_regs[2] = 0;
        return;
    }

    u32 total = size * nmemb;
    if (m_files[idx].is_host && m_files[idx].host_file) {
        std::vector<u8> buf(total);
        size_t read = fread(buf.data(), 1, total, m_files[idx].host_file);
        m_mem.write_block(ptr, buf.data(), (u32)read);
        m_files[idx].offset += (u32)read;
        g_cpu_regs[2] = (u32)(read / size);
    } else {
        u32 available = (u32)m_files[idx].embedded_data.size() - m_files[idx].offset;
        u32 to_read = std::min(total, available);
        if (to_read > 0) {
            m_mem.write_block(ptr, &m_files[idx].embedded_data[m_files[idx].offset], to_read);
            m_files[idx].offset += to_read;
        }
        g_cpu_regs[2] = to_read / size;
    }
}

void Syscalls::impl_fwrite() {
    u32 ptr = arg(0);
    u32 size = arg(1);
    u32 nmemb = arg(2);
    u32 file_handle = arg(3);

    int idx = file_handle;
    if (idx < 0 || idx >= 64 || !m_files[idx].in_use) {
        g_cpu_regs[2] = 0;
        return;
    }

    u32 total = size * nmemb;
    if (m_files[idx].is_host && m_files[idx].host_file) {
        std::vector<u8> buf(total);
        m_mem.read_block(ptr, buf.data(), total);
        size_t written = fwrite(buf.data(), 1, total, m_files[idx].host_file);
        m_files[idx].offset += (u32)written;
        g_cpu_regs[2] = (u32)(written / size);
    } else {
        // Read-only embedded file
        g_cpu_regs[2] = 0;
    }
}

void Syscalls::impl_fopen() {
    u32 path_addr = arg(0);
    u32 mode_addr = arg(1);
    std::string path = guest_string(path_addr);
    std::string mode = guest_string(mode_addr);

    printf("[FOPEN] path=\"%s\" mode=\"%s\"\n", path.c_str(), mode.c_str());

    int idx = alloc_file_handle();
    if (idx < 0) { g_cpu_regs[2] = 0; return; }

    // For Phase 1, all fopen calls return NULL (not implemented)
    // In Phase 3, we'll implement the resource archive loader
    m_files[idx].in_use = false;
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_fclose() {
    u32 file_handle = arg(0);
    close_file_handle(file_handle);
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_fseek() {
    u32 file_handle = arg(0);
    s32 offset = (s32)arg(1);
    u32 whence = arg(2);

    int idx = file_handle;
    if (idx < 0 || idx >= 64 || !m_files[idx].in_use) {
        g_cpu_regs[2] = -1;
        return;
    }

    if (m_files[idx].is_host && m_files[idx].host_file) {
        g_cpu_regs[2] = (u32)fseek(m_files[idx].host_file, offset, (int)whence);
        if (g_cpu_regs[2] == 0) {
            m_files[idx].offset = (u32)ftell(m_files[idx].host_file);
        }
    } else {
        if (whence == 0) {  // SEEK_SET
            m_files[idx].offset = (u32)offset;
        } else if (whence == 1) {  // SEEK_CUR
            m_files[idx].offset += (u32)offset;
        } else if (whence == 2) {  // SEEK_END
            m_files[idx].offset = (u32)m_files[idx].embedded_data.size() + (u32)offset;
        }
        g_cpu_regs[2] = 0;
    }
}

void Syscalls::impl_ftell() {
    u32 file_handle = arg(0);
    int idx = file_handle;
    if (idx < 0 || idx >= 64 || !m_files[idx].in_use) {
        g_cpu_regs[2] = 0;
        return;
    }
    g_cpu_regs[2] = m_files[idx].offset;
}

void Syscalls::impl_fgets() {
    u32 buf = arg(0);
    u32 size = arg(1);
    u32 file_handle = arg(2);

    int idx = file_handle;
    if (idx < 0 || idx >= 64 || !m_files[idx].in_use) {
        g_cpu_regs[2] = 0;
        return;
    }

    u32 i = 0;
    while (i < size - 1) {
        u8 c;
        if (m_files[idx].is_host && m_files[idx].host_file) {
            int ch = fgetc(m_files[idx].host_file);
            if (ch == EOF) break;
            c = (u8)ch;
        } else {
            if (m_files[idx].offset >= m_files[idx].embedded_data.size()) break;
            c = m_files[idx].embedded_data[m_files[idx].offset++];
        }
        m_mem.write_u8(buf + i, c);
        i++;
        if (c == '\n') break;
    }
    m_mem.write_u8(buf + i, 0);
    g_cpu_regs[2] = i > 0 ? buf : 0;
}

void Syscalls::impl_fflush() {
    u32 file_handle = arg(0);
    int idx = file_handle;
    if (idx >= 0 && idx < 64 && m_files[idx].in_use && m_files[idx].is_host && m_files[idx].host_file) {
        fflush(m_files[idx].host_file);
    }
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_feof() {
    u32 file_handle = arg(0);
    int idx = file_handle;
    if (idx < 0 || idx >= 64 || !m_files[idx].in_use) {
        g_cpu_regs[2] = 0;
        return;
    }
    if (m_files[idx].is_host && m_files[idx].host_file) {
        g_cpu_regs[2] = (u32)feof(m_files[idx].host_file);
    } else {
        g_cpu_regs[2] = m_files[idx].offset >= m_files[idx].embedded_data.size() ? 1 : 0;
    }
}

void Syscalls::impl_ferror() {
    u32 file_handle = arg(0);
    int idx = file_handle;
    if (idx < 0 || idx >= 64 || !m_files[idx].in_use) {
        g_cpu_regs[2] = 0;
        return;
    }
    if (m_files[idx].is_host && m_files[idx].host_file) {
        g_cpu_regs[2] = (u32)ferror(m_files[idx].host_file);
    } else {
        g_cpu_regs[2] = 0;
    }
}

void Syscalls::impl_fgetc() {
    u32 file_handle = arg(0);
    int idx = file_handle;
    if (idx < 0 || idx >= 64 || !m_files[idx].in_use) {
        g_cpu_regs[2] = 0xFFFFFFFF;  // EOF
        return;
    }
    if (m_files[idx].is_host && m_files[idx].host_file) {
        g_cpu_regs[2] = (u32)fgetc(m_files[idx].host_file);
    } else {
        if (m_files[idx].offset >= m_files[idx].embedded_data.size()) {
            g_cpu_regs[2] = 0xFFFFFFFF;
        } else {
            g_cpu_regs[2] = m_files[idx].embedded_data[m_files[idx].offset++];
        }
    }
}

void Syscalls::impl_fputc() {
    u32 c = arg(0);
    u32 file_handle = arg(1);
    int idx = file_handle;
    if (idx < 0 || idx >= 64 || !m_files[idx].in_use) {
        g_cpu_regs[2] = 0xFFFFFFFF;
        return;
    }
    if (m_files[idx].is_host && m_files[idx].host_file) {
        g_cpu_regs[2] = (u32)fputc((int)c, m_files[idx].host_file);
    } else {
        g_cpu_regs[2] = 0xFFFFFFFF;  // read-only
    }
}

void Syscalls::impl_setbuf() { g_cpu_regs[2] = 0; }
void Syscalls::impl_setvbuf() { g_cpu_regs[2] = 0; }

void Syscalls::impl_exit() {
    u32 code = arg(0);
    printf("[SYSCALL] exit(%d)\n", code);
    // This will be caught by the emulator main loop
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_atexit() { g_cpu_regs[2] = 0; }

void Syscalls::impl_getenv() {
    u32 name_addr = arg(0);
    std::string name = guest_string(name_addr);
    printf("[GETENV] %s\n", name.c_str());
    g_cpu_regs[2] = 0;  // no environment
}

void Syscalls::impl_strncpy() {
    u32 dest = arg(0);
    u32 src = arg(1);
    u32 n = arg(2);

    std::string s = guest_string(src);
    u32 len = (u32)std::min((size_t)n, s.size());
    for (u32 i = 0; i < len; i++) {
        m_mem.write_u8(dest + i, (u8)s[i]);
    }
    for (u32 i = len; i < n; i++) {
        m_mem.write_u8(dest + i, 0);
    }
    g_cpu_regs[2] = dest;
}

void Syscalls::impl_strncmp() {
    u32 a_addr = arg(0);
    u32 b_addr = arg(1);
    u32 n = arg(2);

    std::string a = guest_string(a_addr);
    std::string b = guest_string(b_addr);

    size_t len = std::min((size_t)n, std::min(a.size(), b.size()));
    int result = 0;
    for (size_t i = 0; i < len; i++) {
        if (a[i] != b[i]) { result = (unsigned char)a[i] - (unsigned char)b[i]; break; }
    }
    if (result == 0 && a.size() != b.size()) {
        result = (a.size() < b.size()) ? -1 : 1;
    }
    g_cpu_regs[2] = (u32)result;
}

void Syscalls::impl_strcpy() {
    u32 dest = arg(0);
    u32 src = arg(1);

    std::string s = guest_string(src);
    for (u32 i = 0; i <= s.size(); i++) {
        m_mem.write_u8(dest + i, (u8)s[i]);
    }
    g_cpu_regs[2] = dest;
}

void Syscalls::impl_strcmp() {
    u32 a_addr = arg(0);
    u32 b_addr = arg(1);

    std::string a = guest_string(a_addr);
    std::string b = guest_string(b_addr);

    int result = strcmp(a.c_str(), b.c_str());
    g_cpu_regs[2] = (u32)result;
}

void Syscalls::impl_strlen() {
    u32 s_addr = arg(0);
    std::string s = guest_string(s_addr);
    g_cpu_regs[2] = (u32)s.size();
}

void Syscalls::impl_memset() {
    u32 dest = arg(0);
    u32 c = arg(1);
    u32 n = arg(2);

    for (u32 i = 0; i < n; i++) {
        m_mem.write_u8(dest + i, (u8)c);
    }
    g_cpu_regs[2] = dest;
}

void Syscalls::impl_memcpy() {
    u32 dest = arg(0);
    u32 src = arg(1);
    u32 n = arg(2);

    std::vector<u8> buf(n);
    m_mem.read_block(src, buf.data(), n);
    m_mem.write_block(dest, buf.data(), n);
    g_cpu_regs[2] = dest;
}

void Syscalls::impl_memmove() {
    // Same as memcpy but handles overlap
    u32 dest = arg(0);
    u32 src = arg(1);
    u32 n = arg(2);

    std::vector<u8> buf(n);
    m_mem.read_block(src, buf.data(), n);
    m_mem.write_block(dest, buf.data(), n);
    g_cpu_regs[2] = dest;
}

void Syscalls::impl_memcmp() {
    u32 a_addr = arg(0);
    u32 b_addr = arg(1);
    u32 n = arg(2);

    std::vector<u8> a(n), b(n);
    m_mem.read_block(a_addr, a.data(), n);
    m_mem.read_block(b_addr, b.data(), n);

    int result = memcmp(a.data(), b.data(), n);
    g_cpu_regs[2] = (u32)result;
}

void Syscalls::impl_strstr() {
    u32 haystack_addr = arg(0);
    u32 needle_addr = arg(1);

    std::string haystack = guest_string(haystack_addr);
    std::string needle = guest_string(needle_addr);

    size_t pos = haystack.find(needle);
    if (pos == std::string::npos) {
        g_cpu_regs[2] = 0;
    } else {
        g_cpu_regs[2] = haystack_addr + (u32)pos;
    }
}

void Syscalls::impl_strcat() {
    u32 dest = arg(0);
    u32 src = arg(1);

    std::string s = guest_string(src);
    u32 i = 0;
    while (m_mem.read_u8(dest + i) != 0) i++;
    for (char c : s) {
        m_mem.write_u8(dest + i++, (u8)c);
    }
    m_mem.write_u8(dest + i, 0);
    g_cpu_regs[2] = dest;
}

void Syscalls::impl_strchr() {
    u32 s_addr = arg(0);
    u32 c = arg(1);

    std::string s = guest_string(s_addr);
    size_t pos = s.find((char)c);
    if (pos == std::string::npos) {
        g_cpu_regs[2] = 0;
    } else {
        g_cpu_regs[2] = s_addr + (u32)pos;
    }
}

void Syscalls::impl_strrchr() {
    u32 s_addr = arg(0);
    u32 c = arg(1);

    std::string s = guest_string(s_addr);
    size_t pos = s.rfind((char)c);
    if (pos == std::string::npos) {
        g_cpu_regs[2] = 0;
    } else {
        g_cpu_regs[2] = s_addr + (u32)pos;
    }
}

void Syscalls::impl_strtok() {
    // Simplified strtok
    u32 delim_addr = arg(1);
    std::string delim = guest_string(delim_addr);

    // Phase 1: return NULL (not fully implemented)
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_sscanf() {
    // Simplified sscanf
    printf("[SSCANF] stub\n");
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_rand() {
    g_cpu_regs[2] = (u32)rand();
}

void Syscalls::impl_srand() {
    u32 seed = arg(0);
    srand(seed);
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_qsort() {
    printf("[QSORT] stub\n");
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_bsearch() {
    printf("[BSEARCH] stub\n");
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_abs() {
    s32 val = (s32)arg(0);
    g_cpu_regs[2] = (u32)(val < 0 ? -val : val);
}

void Syscalls::impl_atoi() {
    u32 s_addr = arg(0);
    std::string s = guest_string(s_addr);
    g_cpu_regs[2] = (u32)atoi(s.c_str());
}

void Syscalls::impl_atof() {
    printf("[ATOF] stub\n");
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_strtol() {
    u32 s_addr = arg(0);
    u32 endptr_addr = arg(1);
    u32 base = arg(2);

    std::string s = guest_string(s_addr);
    char* endptr;
    long val = strtol(s.c_str(), &endptr, (int)base);

    if (endptr_addr) {
        u32 end_offset = (u32)(endptr - s.c_str());
        m_mem.write_u32(endptr_addr, s_addr + end_offset);
    }
    g_cpu_regs[2] = (u32)val;
}

void Syscalls::impl_strtoul() {
    u32 s_addr = arg(0);
    u32 endptr_addr = arg(1);
    u32 base = arg(2);

    std::string s = guest_string(s_addr);
    char* endptr;
    unsigned long val = strtoul(s.c_str(), &endptr, (int)base);

    if (endptr_addr) {
        u32 end_offset = (u32)(endptr - s.c_str());
        m_mem.write_u32(endptr_addr, s_addr + end_offset);
    }
    g_cpu_regs[2] = (u32)val;
}

void Syscalls::impl_strtod() {
    printf("[STRTO] stub\n");
    g_cpu_regs[2] = 0;
}

// === Dingoo OS implementations ===

void Syscalls::impl_fsys_fopen() {
    u32 path_addr = arg(0);
    u32 mode_addr = arg(1);
    std::string path = guest_string(path_addr);
    std::string mode = guest_string(mode_addr);

    printf("[FSYS_FOPEN] path=\"%s\" mode=\"%s\"\n", path.c_str(), mode.c_str());

    int idx = alloc_file_handle();
    if (idx < 0) { g_cpu_regs[2] = 0; return; }

    // Phase 1: return NULL, Phase 3: implement archive loading
    m_files[idx].in_use = false;
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_fsys_fread() { impl_fread(); }
void Syscalls::impl_fsys_fseek() { impl_fseek(); }
void Syscalls::impl_fsys_fclose() { impl_fclose(); }
void Syscalls::impl_fsys_ftell() { impl_ftell(); }
void Syscalls::impl_fsys_fgets() { impl_fgets(); }
void Syscalls::impl_fsys_feof() { impl_feof(); }
void Syscalls::impl_fsys_ferror() { impl_ferror(); }
void Syscalls::impl_fsys_fgetc() { impl_fgetc(); }
void Syscalls::impl_fsys_fputc() { impl_fputc(); }

void Syscalls::impl_lcd_flip() {
    m_display.flip();
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_lcd_set_frame() {
    u32 addr = arg(0);
    m_display.set_frame_addr(addr);
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_LcdGetDisMode() {
    g_cpu_regs[2] = m_display.is_display_on() ? 1 : 0;
}

void Syscalls::impl_kbd_get_key() {
    g_cpu_regs[2] = 0;  // no input in Phase 1
}

void Syscalls::impl_kbd_get_status() {
    g_cpu_regs[2] = 0;  // no keys pressed
}

void Syscalls::impl_waveout_open() {
    printf("[AUDIO] waveout_open() - stub\n");
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_waveout_write() {
    g_cpu_regs[2] = arg(1);  // return size written
}

void Syscalls::impl_waveout_close() {
    printf("[AUDIO] waveout_close() - stub\n");
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_OSTimeGet() {
    g_cpu_regs[2] = 0;  // stub, return 0
}

void Syscalls::impl_OSTimeDly() {
    u32 ticks = arg(0);
    printf("[RTOS] OSTimeDly(%u) - stub\n", ticks);
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_OSSemCreate() {
    u32 initial = arg(0);
    printf("[RTOS] OSSemCreate(%u)\n", initial);
    g_cpu_regs[2] = 1;  // dummy semaphore ID
}

void Syscalls::impl_OSSemPend() {
    u32 sem_id = arg(0);
    u32 timeout = arg(1);
    printf("[RTOS] OSSemPend(%u, %u)\n", sem_id, timeout);
    g_cpu_regs[2] = 0;  // success
}

void Syscalls::impl_OSSemPost() {
    u32 sem_id = arg(0);
    printf("[RTOS] OSSemPost(%u)\n", sem_id);
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_OSSemDel() {
    u32 sem_id = arg(0);
    printf("[RTOS] OSSemDel(%u)\n", sem_id);
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_OSTaskCreate() {
    u32 entry = arg(0);
    u32 arg_val = arg(1);
    printf("[RTOS] OSTaskCreate(entry=0x%08X, arg=0x%08X) - stub\n", entry, arg_val);
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_StartSwTimer() {
    u32 interval = arg(0);
    u32 callback = arg(1);
    printf("[TIMER] StartSwTimer(interval=%u, callback=0x%08X)\n", interval, callback);
    g_cpu_regs[2] = 1;  // dummy timer ID
}

void Syscalls::impl_free_irq() {
    u32 irq = arg(0);
    printf("[IRQ] free_irq(%u)\n", irq);
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_icache_invalidate_all() {
    // No-op
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_dcache_writeback_all() {
    // No-op
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_serial_putc() {
    u32 c = arg(0);
    putchar((char)c);
    fflush(stdout);
    g_cpu_regs[2] = 0;
}

void Syscalls::impl_serial_getc() {
    g_cpu_regs[2] = 0;
}
