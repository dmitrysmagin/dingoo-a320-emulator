#ifndef SYSCALLS_H
#define SYSCALLS_H

#include "types.h"
#include "memory.h"
#include <vector>
#include "display.h"
#include "archive.h"
#include <string>
#include <vector>

class Syscalls {
public:
    Syscalls(Memory& mem, Display& display);

    void dispatch(int got_index, u32 return_addr);
    const char* got_name(int index) const;
    void set_archive(Archive* archive) { m_archive = archive; }

private:
    Memory& m_mem;
    Display& m_display;

    u32 arg(int n);
    std::string guest_string(u32 vaddr);

    // === 72 GOT dispatch implementations ===
    // 0-10: libc
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
    void impl_fseek();
    // 11-23: display / cache
    void impl_LcdGetDisMode();
    void impl_vxGoHome();
    void impl_StartSwTimer();
    void impl_free_irq();
    void impl_fsys_RefreshCache();
    void impl_strlen();
    void impl__lcd_set_frame();
    void impl__lcd_get_frame();
    void impl_lcd_get_cframe();
    void impl_ap_lcd_set_frame();
    void impl_lcd_flip();
    void impl___icache_invalidate_all();
    void impl___dcache_writeback_all();
    // 24-31: media / OS / serial / input
    void impl_TaskMediaFunStop();
    void impl_OSCPUSaveSR();
    void impl_OSCPURestoreSR();
    void impl_serial_getc();
    void impl_serial_putc();
    void impl__kbd_get_status();
    void impl_get_game_vol();
    void impl__kbd_get_key();
    // 32-45: filesystem
    void impl_fsys_fopen();
    void impl_fsys_fread();
    void impl_fsys_fclose();
    void impl_fsys_fseek();
    void impl_fsys_ftell();
    void impl_fsys_remove();
    void impl_fsys_rename();
    void impl_fsys_ferror();
    void impl_fsys_feof();
    void impl_fsys_fwrite();
    void impl_fsys_findfirst();
    void impl_fsys_findnext();
    void impl_fsys_findclose();
    void impl_fsys_flush_cache();
    // 46-48: USB
    void impl_USB_Connect();
    void impl_udc_attached();
    void impl_USB_No_Connect();
    // 49-57: audio
    void impl_waveout_open();
    void impl_waveout_close();
    void impl_waveout_close_at_once();
    void impl_waveout_set_volume();
    void impl_HP_Mute_sw();
    void impl_waveout_can_write();
    void impl_waveout_write();
    void impl_pcm_can_write();
    void impl_pcm_ioctl();
    // 58-67: RTOS
    void impl_OSTimeGet();
    void impl_OSTimeDly();
    void impl_OSSemPend();
    void impl_OSSemPost();
    void impl_OSSemCreate();
    void impl_OSTaskCreate();
    void impl_OSSemDel();
    void impl_OSTaskDel();
    void impl_GetTickCount();
    void impl__sys_judge_event();
    // 68-71: unicode / locale
    void impl_fsys_fopenW();
    void impl___to_unicode_le();
    void impl___to_locale_ansi();
    void impl_get_current_language();

    // Internal helpers
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

    struct FileHandle {
        bool in_use;
        bool is_host;
        bool is_archive;
        FILE* host_file;
        const Archive* archive;
        const ArchiveEntry* archive_entry;
        std::vector<u8> embedded_data;
        u32 offset;
    };
    FileHandle m_files[64];
    int alloc_file_handle();
    void close_file_handle(int idx);

    // Internal implementations for stdlib-style I/O functions
    u32 do_fread(u32 ptr, u32 size, u32 nmemb, u32 file_handle);
    u32 do_fwrite(u32 ptr, u32 size, u32 nmemb, u32 file_handle);
    u32 do_fseek(u32 file_handle, s32 offset, u32 whence);
    u32 do_ftell(u32 file_handle);
    u32 do_feof(u32 file_handle);
    u32 do_ferror(u32 file_handle);

    Archive* m_archive;
    bool m_audio_open;
    u32 m_audio_write_count;
    u32 m_got_call_count;
    u32 m_got_call_counts[72];
    std::vector<u32> m_semaphores;

    // µC/OS-II cooperative task scheduler
    static constexpr int MAX_TASKS = 8;
    struct Task {
        bool active;
        bool blocked;
        u32 regs[32];
        u32 hi, lo;
        u32 task_arg; // original a0 (p_arg) for the task
    };

    // External PC tracking - the GOT dispatch caller (execute_one) saves/restores
    // the guest PC via g_cpu_pc when tasks switch.
    Task m_tasks[MAX_TASKS];
    u32 m_idle_regs[32];
    int m_current_task;
    int m_task_count;

public:
    u32 got_call_count() const { return m_got_call_count; }
    u32 got_call_counts(int i) const { return m_got_call_counts[i]; }
    bool simulate_vsync();  // returns true if task was switched
};

#endif // SYSCALLS_H
