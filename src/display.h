#ifndef DISPLAY_H
#define DISPLAY_H

#include "types.h"
#include <SDL2/SDL.h>
#include <queue>
#include <memory>

// Dingoo A320 key bitmasks for _kbd_get_status / get_key_val.
// Values match the VK_GAME_* constants in gamelib.h (gameplay/gamelib.h):
//   VK_GAME_RIGHT=0x0002, LEFT=0x0004, UP=0x0008, B=0x0010, DOWN=0x0020,
//   A=0x0040, X=0x0100, Y=0x0200, START=0x0400, R=0x0800, L=0x1000, SELECT=0x2000.
enum DingooKey : u32 {
    DKEY_RIGHT  = 0x0002,
    DKEY_LEFT   = 0x0004,
    DKEY_UP     = 0x0008,
    DKEY_B      = 0x0010,
    DKEY_DOWN   = 0x0020,
    DKEY_A      = 0x0040,
    DKEY_X      = 0x0100,
    DKEY_Y      = 0x0200,
    DKEY_START  = 0x0400,
    DKEY_R      = 0x0800,
    DKEY_L      = 0x1000,
    DKEY_SELECT = 0x2000,
};

class Display {
public:
    static constexpr int WIDTH  = 320;
    static constexpr int HEIGHT = 240;
    static constexpr int SCALE  = 3;
    static constexpr int PIXEL_SIZE = 2;  // RGB565

    Display();
    ~Display();

    bool init();
    void shutdown();

    // Framebuffer access
    u16* get_framebuffer() { return m_framebuffer.get(); }
    u32  get_frame_addr() const { return m_frame_addr; }
    u32  get_back_addr()  const { return m_frame_back; }
    void set_back_addr(u32 addr) { m_frame_back = addr; }

    void flip(const u8* guest_ram = nullptr, u32 ram_size = 0);
    // flip() reading WIDTH pixels per row but advancing src_stride bytes between rows (RGB565)
    void flip_strided(const u8* guest_ram, u32 ram_size, u32 src_stride);
    // flip() reading the framebuffer as 32-bit ARGB8888 (4 bytes/pixel, stride WIDTH*4)
    void flip_argb8888(const u8* guest_ram, u32 ram_size);
    // flip() with a secondary overlay buffer: background from frame_addr, text from overlay_phys
    void flip_composite(const u8* guest_ram, u32 ram_size, u32 overlay_phys);
    void set_frame_addr(u32 addr) { m_frame_addr = addr; }

    bool is_display_on() const { return m_display_on; }
    void set_display_on(bool on) { m_display_on = on; }

    bool pump_events();
    void present_blank();    // SDL_RenderPresent without touching dirty flag or framebuffer
    void upload_and_present(); // convert RGB565 → ARGB8888, upload to texture, present
    void save_screenshot(const char* path);
    bool is_dirty() const { return m_dirty; }
    void clear_dirty() { m_dirty = false; }

    u32 get_dingoo_keys() const { return m_dingoo_keys; }
    u32 get_hw_keys() const { return m_hw_keys; }
    void set_key(u32 key, bool down) {
        if (down) m_dingoo_keys |= key;
        else      m_dingoo_keys &= ~key;
    }
    // Queue a Dingoo-button edge (updates DKEY/HW masks and the event FIFOs).
    void inject_dingoo_key(u32 dkey, bool down);

    // Input event queues
    static constexpr u32 EVT_KEY_DOWN = 0x01;
    static constexpr u32 EVT_KEY_UP   = 0x02;
    bool has_input_event() const { return !m_input_events.empty(); }
    u32  pop_input_event();
    bool has_key_event() const { return !m_key_events.empty(); }
    u32  pop_key_event();

    // Convert DKEY_ bitmask to Dingoo SDK key code (A=1..RIGHT=12)
    static u32 bitmask_to_keycode(u32 bitmask);

private:
    SDL_Window*   m_window;
    SDL_Renderer* m_renderer;
    SDL_Texture*  m_texture;

    std::unique_ptr<u16[]> m_framebuffer;
    std::unique_ptr<u32[]> m_argb_cache;   // last ARGB8888 frame written by flip / flip_argb8888
    u32 m_frame_addr;   // physical address of the front (currently displayed) buffer
    u32 m_frame_back;   // physical address of the back (available for rendering) buffer
    bool m_display_on;
    bool m_dirty;
    bool m_initialized;
    bool m_argb_valid;                  // true once m_argb_cache has been populated
    u32 m_dingoo_keys;
    u32 m_hw_keys;
    u32 m_prev_dingoo_keys;
    std::queue<u32> m_input_events;
    std::queue<u32> m_key_events;
    static constexpr size_t MAX_INPUT_EVENTS = 64;
};

#endif // DISPLAY_H
