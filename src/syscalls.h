#ifndef SYSCALLS_H
#define SYSCALLS_H

#include "types.h"
#include "memory.h"
#include "cop0.h"
#include <vector>
#include "display.h"
#include "archive.h"
#include <string>
#include <vector>
#include <queue>
#include <SDL2/SDL.h>
#include <dirent.h>

class Syscalls {
public:
    Syscalls(Memory& mem, Display& display);

    void dispatch(int got_index, u32 return_addr);
    const char* got_name(int index) const;
    bool got_is_stub(int index) const;
    void set_archive(Archive* archive) { m_archive = archive; }
    void set_app_path(const char* path) { m_app_path = path ? path : ""; }
    void set_cop0(COP0* cop0) { m_cop0 = cop0; }

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
    // 72-76: resource / dl (only brick.app has these)
    void impl_get_dl_handle();
    void impl_dl_res_open();
    void impl_dl_res_get_size();
    void impl_dl_res_get_data();
    void impl_dl_res_close();

    // GOT 77-86: LCD/input wrappers and µC/GUI helpers
    void impl_lcd_set_frame();
    void impl_lcd_get_frame();
    void impl_lcd_get_bpp();
    void impl_LCD_GetXSize();
    void impl_LCD_GetYSize();
    void impl_LCD_Color2Index();
    void impl_kbd_get_key();
    void impl_kbd_get_status();
    void impl_sys_judge_event();
    void impl_open_gui_key_msg();

    std::string format_string(const std::string& fmt, int first_arg);

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
    COP0* m_cop0 = nullptr;
    std::string m_app_path;
    u32 m_lcd_bpp;       // 1=indexed, 2=RGB565, 4=ARGB8888; never stored in guest RAM
    bool m_audio_open;
    bool m_audio_device_open;
    u32 m_audio_write_count;
    SDL_AudioDeviceID m_audio_device;
    SDL_mutex* m_audio_mutex;
    std::queue<s16> m_audio_queue;
    float m_volume;
    u32 m_pcm_volume = 128;
    u32 m_got_call_count;
    u32 m_got_call_counts[MAX_GOT_ENTRIES];
    std::vector<u32> m_semaphores;

    // Frame buffer pool for format conversion (ARGB8888→RGB565)
    static constexpr int FB_POOL_SIZE = 4;
    struct FbEntry {
        u32 phys;
        u32 size;
        bool in_use;
    };
    FbEntry m_fb_pool[FB_POOL_SIZE];
    u32 allocate_fb(u32 size);
    void release_fb(u32 phys);
    void argb8888_to_rgb565(const u8* src, u8* dst, u32 pixel_count);

    // get_dl_handle counter (incrementing handle allocator)
    u32 m_dl_handle_counter = 1;

    // dl_res handle tracking (for brick.app etc.)
    static constexpr int MAX_DL_RES = 32;
    struct DlResHandle {
        bool in_use;
        const ArchiveEntry* entry;
    };
    DlResHandle m_dl_res[MAX_DL_RES];
    int alloc_dl_res_handle();
    void free_dl_res_handle(int idx);

    // Dingoo key code table (input.md reference: A=0x01..RIGHT=0x0C)
    static u32 bitmask_to_keycode(u32 bitmask);

    // Event queue physical address (used by _sys_judge_event)
    static constexpr u32 EVENT_QUEUE_ADDR = 0x80BFECD8;
    static constexpr u32 EVENT_TYPE_DOWN  = 0x01;
    static constexpr u32 EVENT_TYPE_UP    = 0x02;

    // Dingoo µC/OS-II kernel keyboard state mailbox.
    // Real firmware: keyboard ISR writes current key mask here every tick.
    // `_kbd_get_status` updates this before returning, and games that poll
    // directly (e.g. 7days event dispatcher at 0x80A000FC) read from this
    // fixed kernel ABI address. Computed as kernel_GP - 0x62F8 where
    // kernel_GP = 0x80B40000.
    static constexpr u32 KERNEL_KEY_STATE_ADDR = 0x80B39D08;

    u32 m_prev_kbd_keys = 0;
    u32 m_prev_hw = 0;

    // µC/OS-II cooperative task scheduler
    static constexpr int MAX_TASKS = 64;
    struct Task {
        bool active;
        bool blocked;
        u32 regs[32];
        u32 hi, lo;
        u32 pc;        // resume PC (separate from $ra to avoid corruption on preemption)
        u32 task_arg;  // original a0 (p_arg) for the task
        u8  task_prio; // µC/OS-II task priority
        u32 wake_tick; // >0 = OSTimeDly blocks until this tick count
        u32 block_sem; // semaphore ECB addr task is blocked on (0 = not sem-blocked)
        u32 sem_err_ptr; // *err to write on sem wake (0 = OS_NO_ERR, 10 = OS_TIMEOUT)
    };

    // External PC tracking - the GOT dispatch caller (execute_one) saves/restores
    // the guest PC via g_cpu_pc when tasks switch.
    Task m_tasks[MAX_TASKS];
    u32 m_idle_regs[32];
    u32 m_idle_hi, m_idle_lo;
    int m_current_task;
    int m_task_count;
    u32 m_os_ticks;
    u32 m_start_tick;
    bool m_scheduler_started;
    bool m_task_switched;
    bool m_in_idle;
    u32 m_idle_pc;

    void save_current_task();
    void switch_to_task(int task_idx);
    int find_ready_task();

public:
    // Audio
    void shutdown_audio();
    static void SDLCALL audio_callback(void* userdata, Uint8* stream, int len);
    u32 got_call_count() const { return m_got_call_count; }
    u32 got_call_counts(int i) const { return m_got_call_counts[i]; }
    bool simulate_vsync();
    void set_idle_regs(const u32 regs[32]);
    void set_idle_pc(u32 pc) { m_idle_pc = pc; }
    void register_main_context(u32 pc, u32 a0, u8 prio);
    bool in_idle() const { return m_in_idle; }
    bool task_switched() const { return m_task_switched; }
    void clear_task_switched() { m_task_switched = false; }

    // Software timer (StartSwTimer / timer-processing loop)
    struct TimerEntry {
        bool active;
        u32 period_ms;
        u32 callback;
        u32 elapsed; // cumulative ms elapsed
    };
    std::vector<TimerEntry> m_timers;
    void process_timers();
    void call_guest_function(u32 func, u32 arg0);
    u32 m_last_timer_tick;

    // Directory search handles (fsys_findfirst/next/close)
    struct SearchEntry {
        bool in_use;
        DIR* dir;
        int filter;
        std::string dir_path;
    };
    std::vector<SearchEntry> m_searches;
    int alloc_search_handle();
    void free_search_handle(int idx);
};

#endif // SYSCALLS_H
