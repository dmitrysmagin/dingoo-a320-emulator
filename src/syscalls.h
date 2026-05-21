#ifndef SYSCALLS_H
#define SYSCALLS_H

#include "types.h"
#include "memory.h"
#include "display.h"
#include <string>
#include <vector>

class Syscalls {
public:
    Syscalls(Memory& mem, Display& display);

    // Called when a JAL targets a GOT trampoline
    void dispatch(int got_index, u32 return_addr);

    // Get the name of a GOT function
    const char* got_name(int index) const;

private:
    Memory& m_mem;
    Display& m_display;

    // Read argument from stack or register
    u32 arg(int n);  // $a0..$a3 or stack

    // Helper: read guest string
    std::string guest_string(u32 vaddr);

    // === Implementation functions ===

    // libc / memory
    void impl_abort();
    void impl_printf();
    void impl_sprintf();
    void impl_fprintf();
    void impl_strncasecmp();
    void impl_malloc();
    void impl_realloc();
    void impl_free();
    void impl_fread();
    void impl_fwrite();
    void impl_fopen();
    void impl_fclose();
    void impl_fseek();
    void impl_ftell();
    void impl_fgets();
    void impl_fflush();
    void impl_feof();
    void impl_ferror();
    void impl_fgetc();
    void impl_fputc();
    void impl_setbuf();
    void impl_setvbuf();
    void impl_exit();
    void impl_atexit();
    void impl_getenv();
    void impl_strncpy();
    void impl_strncmp();
    void impl_strcpy();
    void impl_strcmp();
    void impl_strlen();
    void impl_memset();
    void impl_memcpy();
    void impl_memmove();
    void impl_memcmp();
    void impl_strstr();
    void impl_strcat();
    void impl_strchr();
    void impl_strrchr();
    void impl_strtok();
    void impl_sscanf();
    void impl_rand();
    void impl_srand();
    void impl_qsort();
    void impl_bsearch();
    void impl_abs();
    void impl_atoi();
    void impl_atof();
    void impl_strtol();
    void impl_strtoul();
    void impl_strtod();

    // Dingoo OS
    void impl_fsys_fopen();
    void impl_fsys_fread();
    void impl_fsys_fseek();
    void impl_fsys_fclose();
    void impl_fsys_ftell();
    void impl_fsys_fgets();
    void impl_fsys_feof();
    void impl_fsys_ferror();
    void impl_fsys_fgetc();
    void impl_fsys_fputc();
    void impl_lcd_flip();
    void impl_lcd_set_frame();
    void impl_LcdGetDisMode();
    void impl_kbd_get_key();
    void impl_kbd_get_status();
    void impl_waveout_open();
    void impl_waveout_write();
    void impl_waveout_close();
    void impl_OSTimeGet();
    void impl_OSTimeDly();
    void impl_OSSemCreate();
    void impl_OSSemPend();
    void impl_OSSemPost();
    void impl_OSSemDel();
    void impl_OSTaskCreate();
    void impl_StartSwTimer();
    void impl_free_irq();
    void impl_icache_invalidate_all();
    void impl_dcache_writeback_all();
    void impl_serial_putc();
    void impl_serial_getc();

    // Internal heap
    struct HeapBlock {
        u32 addr;
        u32 size;
        bool free;
    };
    std::vector<HeapBlock> m_heap;
    u32 m_heap_top;

    u32 heap_alloc(u32 size);
    void heap_free(u32 addr);
    u32 heap_realloc(u32 addr, u32 new_size);

    // Internal file handles
    struct FileHandle {
        bool in_use;
        bool is_host;
        FILE* host_file;
        std::vector<u8> embedded_data;
        u32 offset;
    };
    FileHandle m_files[64];
    int alloc_file_handle();
    void close_file_handle(int idx);

    u32 m_got_call_count;
public:
    u32 got_call_count() const { return m_got_call_count; }
};

#endif // SYSCALLS_H
