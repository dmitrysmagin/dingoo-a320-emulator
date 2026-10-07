#ifndef SYSCALLS_H
#define SYSCALLS_H

#include "types.h"
#include "memory.h"
#include "cop0.h"
#include "app_parser.h"
#include <vector>
#include "display.h"
#include "archive.h"
#include <string>
#include <vector>
#include <atomic>
#include <memory>
#include <SDL2/SDL.h>
#include <dirent.h>

class Syscalls {
public:
    Syscalls(Memory& mem, Display& display);

    void dispatch(int got_index, u32 return_addr);
    const char* got_name(int index) const;
    bool got_is_stub(int index) const;
    void init_slot_handlers(const std::vector<ImportEntry>& imports);
    void set_archive(Archive* archive) { m_archive = archive; }
    void set_app_path(const char* path);
    void set_guest_image(u32 load_addr, u32 rawd_size);
    void set_cop0(COP0* cop0) { m_cop0 = cop0; }
    void set_nosound(bool v) { m_nosound = v; }
    void set_audio_target_latency_ms(int ms);

    static constexpr int AUDIO_TARGET_LATENCY_MS_DEFAULT = 80;
    static constexpr int AUDIO_TARGET_LATENCY_MS_MIN = 20;
    static constexpr int AUDIO_TARGET_LATENCY_MS_MAX = 500;

private:
    Memory& m_mem;
    Display& m_display;

    u32 arg(int n);
    std::string guest_string(u32 vaddr);
    std::string read_guest_path(u32 vaddr);

    // uOS2 current directory ↔ host "home/<game>/"
    std::string normalize_guest_fs_path(const std::string& guest) const;
    std::string host_path_from_guest(const std::string& guest) const;
    bool ensure_host_dir(const std::string& dir);
    bool ensure_host_parent(const std::string& host_file);
    FILE* open_home_file(const std::string& guest_path, const std::string& mode,
                         std::string* resolved);

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
    void impl_waveout_reset();
    void impl_waveout_get_volume();
    void impl_pcm_can_write();
    void impl_pcm_can_read();
    void impl_pcm_read();
    void impl_pcm_write();
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
    bool gui_dispatch_pending_key();
    static u32 dingoo_code_to_gui_key(u32 code);

    // Non‑standard GOT apps (Yi‑Chi, Overlord‑Fighter, etc.)
    void impl_cmGetSysVersion();
    void impl_cmGetSysModel();
    void cm_write_sys_string(const char* text, u32 ascii_addr);
    void impl_mdelay();
    void impl_fsys_clearerr();
    void impl_OSQCreate();
    void impl_OSFlagPost();
    void impl_SysEnableShutDownPower();
    void impl_SysDisableCloseBkLight();
    void impl_GUI_Lock();
    void impl_GUI_Unlock();
    void impl_GUI_TIMER_SetPeriod();
    void impl_GUI_TIMER_Restart();
    void impl_GUI_TIMER_Delete();
    void impl_WM__SendMessage();
    void impl_WM_DefaultProc();
    void impl_GUI_TIMER_Create();
    void impl_WM_SelectWindow();
    void impl_WM_CreateWindow();
    void impl_WM_DeleteWindow();
    void impl_GUI_Exec();
    void impl_WM_SetFocus();
    void impl_spin_lock_irqsave();
    void impl_spin_unlock_irqrestore();
    void impl_jz_pm_pllconvert();
    void impl_dl_load();
    void impl_dl_free();
    void impl_U8TOU16();
    void impl_U8TOU32();

    // Stubs for remaining Dingoo OS APIs
    void impl_Custom_Memsic_test();
    void impl_GUI_TIMER_Exec();
    void impl_Get_X();
    void impl_Get_Y();
    void impl_Memsic_SerialCommInit();
    void impl_Read_Acc();
    void impl_Read_Acc0();
    void impl_SysDisableBkLight();
    void impl__tcscmp();
    void impl__tcscpy();
    void impl__waveout_open();
    void impl__waveout_set_volume();
    void impl_av_begin_thread();
    void impl_av_create_flag();
    void impl_av_create_sem();
    void impl_av_delay();
    void impl_av_destroy_flag();
    void impl_av_destroy_sem();
    void impl_av_end_thread();
    void impl_av_give_flag();
    void impl_av_give_sem();
    void impl_av_queue_abort();
    void impl_av_queue_end();
    void impl_av_queue_flush();
    void impl_av_queue_get();
    void impl_av_queue_init();
    void impl_av_queue_put();
    void impl_av_reg_object();
    void impl_av_resize_packet();
    void impl_av_uft8_2_unicode();
    void impl_av_unreg_object();
    void impl_av_upper_4cc();
    void impl_av_wait_flag();
    void impl_av_wait_sem();
    void impl_av_wait_sem2();
    void impl_delay_ms();
    void impl_detect_clock();
    void impl_dl_get_proc();
    void impl_fsys_fcloseW();
    void impl_fsys_fclose_flash();
    void impl_fsys_fopen_flash();
    void impl_fsys_mkdir();
    void impl_fsys_removeW();
    void impl_fsys_renameW();
    void impl_isTVON();
    void impl_memcpy();
    void impl_memset();
    void impl_serial_puts();
    void impl_sscanf();
    void impl_sys_get_ccpmp_config();
    void impl_tv_close();
    void impl_tv_disable_switch();
    void impl_tv_enable_switch();
    void impl_tv_get_closeflag();
    void impl_tv_get_openflag();
    void impl_tv_open();
    void impl_tv_set_closeflag();
    void impl_tv_set_openflag();
    void impl_udelay();
    void impl_vsprintf();

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
    u32 m_guest_image_phys;
    u32 m_guest_image_size;
    COP0* m_cop0 = nullptr;
    std::string m_app_path;
    std::string m_home_dir; // host path for the guest's uOS2 cwd
    u32 m_lcd_bpp;        // 1=indexed, 2=RGB565, 4=ARGB8888; never stored in guest RAM
    u32 m_lcd_hw_buf[2]; // phys start of the two HW frame buffers (0 = unallocated)
    bool m_lcd_back;      // which buffer is currently the back buffer
    u32 m_lcd_pending_buf; // phys buffer last returned by _lcd_get_frame (for _lcd_set_frame)
    bool m_nosound;
    bool m_audio_open;
    bool m_audio_device_open;
    u32 m_audio_write_count;
    u32 m_audio_sample_rate;
    u32 m_audio_channels;
    u32 m_audio_bits;
    u8  m_volume_level;
    bool m_audio_paused;
    bool m_audio_muted;
    SDL_AudioDeviceID m_audio_device;
    // SPSC ring: producer (CPU) / consumer (SDL callback), lock-free indices.
    // Target latency: max PCM the guest may queue ahead of playback.
    // Ring is sized slightly larger so the physical buffer is not the bottleneck.
    static constexpr int AUDIO_RING_MS = 120;
    static constexpr int AUDIO_MIN_RING_CAP = 1024;
    static constexpr int AUDIO_MAX_RING_CAP = 32768;
    static constexpr int AUDIO_MAX_CHUNK_SAMPLES = 4096;
    int m_audio_target_latency_ms;
    u32 m_ring_cap;
    u32 m_ring_mask;
    std::unique_ptr<s16[]> m_ring_buf;
    std::unique_ptr<s16[]> m_audio_scratch;
    std::atomic<uint32_t> m_ring_head;
    std::atomic<uint32_t> m_ring_tail;
    std::atomic<uint64_t> m_samples_played;
    std::atomic<uint64_t> m_samples_written;
    std::atomic<bool> m_audio_space_flag;
    float m_volume;
    u32 m_pcm_volume = 128;
    u32 m_audio_underruns;
    u32 m_audio_overruns;
    u32 m_audio_high_water;
    u32 m_audio_block_count;
    u32 m_audio_unblock_count;
    u32 m_audio_sem_post_count;
    int m_last_waveout_bytes;
    u32 m_audio_hw_chunk_bytes;
    uint64_t m_audio_post_watermark;
    static constexpr int AUDIO_SEM_REG_MAX = 4;
    u32 m_audio_sem_reg[AUDIO_SEM_REG_MAX];
    int m_audio_sem_reg_count;
    bool m_audio_has_data;
    u32 m_audio_start_tick;

    int audio_calc_ring_cap(u32 rate, u32 channels) const;
    void audio_alloc_ring(u32 rate, u32 channels);
    void audio_reset_ring();
    int audio_ring_used() const;
    int audio_ring_free() const;
    u32 audio_max_ahead_samples() const;
    int audio_can_write_bytes() const;
    int audio_push_pcm_once(const s16* data, int sample_count);
    bool audio_open_device(int sample_rate, int channels);
    void audio_drain_and_close();
    void audio_close_immediate();
    void audio_write_os_state(bool playing);
    void audio_wake_waiters();
    float audio_gain() const;
    int audio_guest_sample_count(u32 byte_size) const;
    void audio_read_guest_pcm(u32 buf_addr, u32 byte_size, s16* out, int max_samples) const;
    int audio_do_write(u32 buf_addr, u32 byte_size);
    void audio_set_volume_level(u32 vol);
    bool audio_try_complete_blocked_writes();
    bool audio_block_task_for_write(int sample_count);
    int m_audio_block_task;
    int m_audio_block_samples;
    std::unique_ptr<s16[]> m_audio_block_pcm;
    u32 m_got_call_count;
    u32 m_got_call_counts[MAX_GOT_ENTRIES];
    int m_last_got_dispatch_index = -1;
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

    // dl_res handle tracking (SPK resources opened by path or 24-bit key)
    static constexpr int MAX_DL_RES = 64;
    struct DlResHandle {
        bool in_use;
        u32 guest_addr;      // lazy KSEG0 map; 0 until a caller needs a pointer
        u32 size;
        u32 offset;          // sequential read cursor for dl_res_get_data
        const u8* host_data; // archive bytes; open does not copy these into guest RAM
    };
    DlResHandle m_dl_res[MAX_DL_RES];

    static constexpr int MAX_DL_MODULES = 16;
    struct DlModule {
        bool in_use;
        u32 guest_addr;
        u32 size;
        std::string name;
    };
    DlModule m_dl_modules[MAX_DL_MODULES];
    const ArchiveEntry* find_dl_file(const std::string& name) const;
    int alloc_dl_res_handle();
    void free_dl_res_handle(int idx);
    const ArchiveEntry* resolve_dl_res_entry(u32 key_or_path);

    void sem_pend(u32 sem_ptr, u32 timeout, u32 err_ptr);
    bool sem_signal(u32 sem_ptr);
    void audio_register_sem(u32 sem_ptr);
    void audio_post_buffer_sems();
    void audio_post_if_chunks_played();

    // Per-slot handler index: maps GOT slot → index into static s_handlers[]
    std::vector<int> m_slot_handlers;

    // Static handler registry (sorted by name, binary-searched at init)
    struct GOTHandler {
        const char* name;
        void (Syscalls::*handler)();
        bool is_stub;
    };
    static const GOTHandler s_handlers[];
    static const int s_handler_count;
    static int find_handler(const char* name);

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
        u32 task_entry; // original entry point; used if saved pc is invalid
        u32 task_arg;  // original a0 (p_arg) for the task
        u8  task_prio; // µC/OS-II task priority
        u32 wake_tick; // >0 = OSTimeDly blocks until this tick count
        u32 block_sem; // semaphore ECB addr task is blocked on (0 = not sem-blocked)
        u32 sem_err_ptr; // *err to write on sem wake (0 = OS_NO_ERR, 10 = OS_TIMEOUT)
        bool block_audio; // blocked inside waveout_write waiting for ring space
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
    int last_got_dispatch_index() const { return m_last_got_dispatch_index; }
    u32 got_call_counts(int i) const { return m_got_call_counts[i]; }
    bool simulate_vsync();
    void set_idle_regs(const u32 regs[32]);
    void set_idle_pc(u32 pc) { m_idle_pc = pc; }
    u32  idle_pc() const { return m_idle_pc; }
    bool has_blocked_tasks() const;
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

    // === Minimal µC/GUI window manager (Overlord-Fighter, Yi-Chi) ===
    // The app creates one full-screen window with a callback, then polls GUI_Exec()
    // in its main loop.  GUI_Exec drives the message pump: it delivers WM_CREATE once
    // and afterwards fires due GUI timers, which in turn post WM_TIMER back to the
    // window callback via WM__SendMessage.  Message IDs follow µC/GUI.
    static constexpr int WM_MSG_CREATE = 1;
    static constexpr int WM_MSG_KEY    = 14;
    static constexpr int WM_MSG_TIMER  = 0x113;
    static constexpr u32 WM_MAIN_HWIN  = 1;
    // µC/GUI GUI_KEY_* (GUI.h) — Overlord's menu switch and hold-table use these.
    static constexpr u32 GUI_KEY_ENTER  = 13;
    static constexpr u32 GUI_KEY_LEFT   = 16;
    static constexpr u32 GUI_KEY_UP     = 17;
    static constexpr u32 GUI_KEY_RIGHT  = 18;
    static constexpr u32 GUI_KEY_DOWN   = 19;
    static constexpr u32 GUI_KEY_ESCAPE = 27;
    static constexpr u32 GUI_KEY_SPACE  = 32;
    struct GuiTimer {
        bool active;
        u32  callback;
        u32  context;
        u32  period_ms;
        u32  next_due;
    };
    std::vector<GuiTimer> m_gui_timers;
    u32  m_wm_callback;
    bool m_wm_paint_pending;   // WM_CREATE queued for the next GUI_Exec
    u32  m_wm_msg_buf;         // guest-side scratch WM_MESSAGE
    u32  m_gui_timer_msg_buf;  // guest-side scratch GUI_TIMER_MESSAGE
    u32  m_wm_key_info_buf;    // guest-side WM_KEY_INFO { int Key; int PressedCnt; }
    bool m_gui_key_msg_open;   // open_gui_key_msg has armed the key→WM_KEY bridge
    u32  wm_scratch(u32& slot, u32 size);
    void wm_dispatch(int msg_id, u32 data);
    bool gui_run_due_timer();

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
