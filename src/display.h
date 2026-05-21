#ifndef DISPLAY_H
#define DISPLAY_H

#include "types.h"
#include <SDL2/SDL.h>

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

    // Called by lcd_flip syscall
    void flip(const u8* guest_ram = nullptr, u32 ram_size = 0);

    // Called by _lcd_set_frame
    void set_frame_addr(u32 addr) { m_frame_addr = addr; }

    // Called by LcdGetDisMode
    bool is_display_on() const { return m_display_on; }
    void set_display_on(bool on) { m_display_on = on; }

    // Event processing (returns true if should quit)
    bool pump_events();

    // Dirty flag
    bool is_dirty() const { return m_dirty; }
    void clear_dirty() { m_dirty = false; }

private:
    SDL_Window*   m_window;
    SDL_Renderer* m_renderer;
    SDL_Texture*  m_texture;

    u16 m_framebuffer[WIDTH * HEIGHT];
    u32 m_frame_addr;
    bool m_display_on;
    bool m_dirty;
    bool m_initialized;
};

#endif // DISPLAY_H
