#ifndef DISPLAY_H
#define DISPLAY_H

#include "types.h"
#include <SDL2/SDL.h>

// Dingoo A320 key codes (bitmask for _kbd_get_status)
enum DingooKey : u32 {
    DKEY_UP     = 0x001,
    DKEY_DOWN   = 0x002,
    DKEY_LEFT   = 0x004,
    DKEY_RIGHT  = 0x008,
    DKEY_A      = 0x010,
    DKEY_B      = 0x020,
    DKEY_X      = 0x040,
    DKEY_Y      = 0x080,
    DKEY_L      = 0x100,
    DKEY_R      = 0x200,
    DKEY_START  = 0x400,
    DKEY_SELECT = 0x800,
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
    u16* get_framebuffer() { return m_framebuffer; }
    u32  get_frame_addr() const { return m_frame_addr; }

    void flip(const u8* guest_ram = nullptr, u32 ram_size = 0);
    void set_frame_addr(u32 addr) { m_frame_addr = addr; }

    bool is_display_on() const { return m_display_on; }
    void set_display_on(bool on) { m_display_on = on; }

    bool pump_events();
    bool is_dirty() const { return m_dirty; }
    void clear_dirty() { m_dirty = false; }

    u32 get_dingoo_keys() const { return m_dingoo_keys; }

private:
    SDL_Window*   m_window;
    SDL_Renderer* m_renderer;
    SDL_Texture*  m_texture;

    u16 m_framebuffer[WIDTH * HEIGHT];
    u32 m_frame_addr;
    bool m_display_on;
    bool m_dirty;
    bool m_initialized;
    u32 m_dingoo_keys;
};

#endif // DISPLAY_H
