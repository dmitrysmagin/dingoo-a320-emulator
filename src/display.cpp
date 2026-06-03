#include "display.h"
#include <cstdio>
#include <cstring>
#include <cstdint>

Display::Display()
    : m_window(nullptr)
    , m_renderer(nullptr)
    , m_texture(nullptr)
    , m_frame_addr(0)
    , m_frame_back(0)
    , m_display_on(true)
    , m_dirty(false)
    , m_initialized(false)
    , m_argb_valid(false)
    , m_dingoo_keys(0)
    , m_hw_keys(0)
    , m_prev_dingoo_keys(0)
{
    memset(m_framebuffer, 0, sizeof(m_framebuffer));
    memset(m_argb_cache,  0, sizeof(m_argb_cache));
}

Display::~Display() {
    shutdown();
}

bool Display::init() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "[SDL] Failed to init: %s\n", SDL_GetError());
        return false;
    }

    m_window = SDL_CreateWindow(
        "7days - Dingoo A320 Emulator",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WIDTH * SCALE, HEIGHT * SCALE,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    if (!m_window) {
        fprintf(stderr, "[SDL] Failed to create window: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!m_renderer) {
        // Offscreen/dummy driver doesn't support hardware acceleration — try software
        m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!m_renderer) {
        fprintf(stderr, "[SDL] Failed to create renderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(m_window);
        SDL_Quit();
        return false;
    }

    // Use ARGB8888 for the SDL texture: RGB565 on Windows D3D backends maps to
    // DXGI_FORMAT_B5G6R5_UNORM (B in high bits), which swaps R and B vs our RGB565 layout.
    // Some D3D drivers also lack native 16-bit texture support, causing the data to be
    // reinterpreted with wrong stride and producing 4-copies artefacts.  ARGB8888 is
    // universally supported without ambiguity on all SDL backends.
    m_texture = SDL_CreateTexture(
        m_renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        WIDTH, HEIGHT
    );
    if (!m_texture) {
        fprintf(stderr, "[SDL] Failed to create texture: %s\n", SDL_GetError());
        SDL_DestroyRenderer(m_renderer);
        SDL_DestroyWindow(m_window);
        SDL_Quit();
        return false;
    }
    printf("[DISPLAY] SDL2 initialized: %dx%d (scale %d)\n", WIDTH * SCALE, HEIGHT * SCALE, SCALE);

    m_initialized = true;
    return true;
}

void Display::shutdown() {
    if (m_texture) { SDL_DestroyTexture(m_texture); m_texture = nullptr; }
    if (m_renderer) { SDL_DestroyRenderer(m_renderer); m_renderer = nullptr; }
    if (m_window) { SDL_DestroyWindow(m_window); m_window = nullptr; }
    if (m_initialized) { SDL_Quit(); m_initialized = false; }
}

void Display::flip(const u8* guest_ram, u32 ram_size) {
    if (!m_initialized) return;

    m_display_on = true;
    m_dirty = true;

    constexpr u32 fb_bytes = WIDTH * HEIGHT * PIXEL_SIZE;
    u32 phys = m_frame_addr & 0x1FFFFFFF;
    if (phys + fb_bytes <= ram_size)
        memcpy(m_framebuffer, &guest_ram[phys], fb_bytes);

    upload_and_present();
}

void Display::flip_strided(const u8* guest_ram, u32 ram_size, u32 src_stride) {
    if (!m_initialized) return;
    m_display_on = true;
    m_dirty = true;

    u32 phys = m_frame_addr & 0x1FFFFFFF;
    constexpr u32 row_bytes = WIDTH * PIXEL_SIZE;
    if (guest_ram && phys + src_stride * (HEIGHT - 1) + row_bytes <= ram_size) {
        for (int y = 0; y < HEIGHT; y++)
            memcpy(&m_framebuffer[y * WIDTH], &guest_ram[phys + y * src_stride], row_bytes);
    }
    upload_and_present();
}

void Display::flip_argb8888(const u8* guest_ram, u32 ram_size) {
    if (!m_initialized) return;
    m_display_on = true;
    m_dirty = true;

    u32 phys = m_frame_addr & 0x1FFFFFFF;
    constexpr u32 src_stride = WIDTH * 4;
    if (guest_ram && phys + src_stride * HEIGHT <= ram_size) {
        for (int y = 0; y < HEIGHT; y++) {
            const u8* src = &guest_ram[phys + y * src_stride];
            for (int x = 0; x < WIDTH; x++) {
                // JZ4740 framebuffer: 0x00RRGGBB in the 32-bit word → bytes [B, G, R, 0] in LE memory.
                // Confirmed by lcdtest.c: LCD_RED=0x00FF0000, LCD_GREEN=0x0000FF00, LCD_BLUE=0x000000FF.
                u8 b = src[x*4 + 0];
                u8 g = src[x*4 + 1];
                u8 r = src[x*4 + 2];
                m_argb_cache[y * WIDTH + x] = (0xFFu << 24) | ((u32)r << 16) | ((u32)g << 8) | b;
            }
        }
        m_argb_valid = true;
        SDL_UpdateTexture(m_texture, nullptr, m_argb_cache, WIDTH * sizeof(u32));
    }
    SDL_RenderClear(m_renderer);
    SDL_RenderCopy(m_renderer, m_texture, nullptr, nullptr);
    SDL_RenderPresent(m_renderer);
}

void Display::flip_composite(const u8* guest_ram, u32 ram_size, u32 overlay_phys) {
    if (!m_initialized) return;

    m_display_on = true;
    m_dirty = true;

    constexpr u32 fb_bytes = WIDTH * HEIGHT * PIXEL_SIZE;
    u32 bg_phys = m_frame_addr & 0x1FFFFFFF;

    // Copy background layer
    if (guest_ram && bg_phys + fb_bytes <= ram_size)
        memcpy(m_framebuffer, &guest_ram[bg_phys], fb_bytes);

    // Overlay text layer: copy non-zero pixels from overlay_phys on top
    if (guest_ram && overlay_phys && overlay_phys + fb_bytes <= ram_size) {
        const u16* overlay = reinterpret_cast<const u16*>(&guest_ram[overlay_phys]);
        for (int i = 0; i < WIDTH * HEIGHT; i++) {
            if (overlay[i])
                m_framebuffer[i] = overlay[i];
        }
    }

    upload_and_present();
}

// Convert m_framebuffer (RGB565) to ARGB8888 and upload to the SDL texture.
// RGB565 layout: R[15:11] G[10:5] B[4:0].
// Bits are replicated into the vacated LSBs so 0x1F → 0xFF (not 0xF8).
void Display::upload_and_present() {
    static u32 argb[WIDTH * HEIGHT];
    for (int i = 0; i < WIDTH * HEIGHT; i++) {
        u16 px = m_framebuffer[i];
        u8 r5 = (px >> 11) & 0x1F;
        u8 g6 = (px >>  5) & 0x3F;
        u8 b5 = (px      ) & 0x1F;
        u8 r = (r5 << 3) | (r5 >> 2);
        u8 g = (g6 << 2) | (g6 >> 4);
        u8 b = (b5 << 3) | (b5 >> 2);
        argb[i] = (0xFFu << 24) | ((u32)r << 16) | ((u32)g << 8) | b;
    }
    SDL_UpdateTexture(m_texture, nullptr, argb, WIDTH * sizeof(u32));
    SDL_RenderClear(m_renderer);
    SDL_RenderCopy(m_renderer, m_texture, nullptr, nullptr);
    SDL_RenderPresent(m_renderer);
}

static u32 sdl_to_dingoo(SDL_Keycode sym) {
    switch (sym) {
    case SDLK_UP:       return DKEY_UP;
    case SDLK_DOWN:     return DKEY_DOWN;
    case SDLK_LEFT:     return DKEY_LEFT;
    case SDLK_RIGHT:    return DKEY_RIGHT;
    case SDLK_z:        return DKEY_A;
    case SDLK_x:        return DKEY_B;
    case SDLK_a:        return DKEY_X;
    case SDLK_s:        return DKEY_Y;
    case SDLK_q:        return DKEY_L;
    case SDLK_w:        return DKEY_R;
    case SDLK_RETURN:   return DKEY_START;
    case SDLK_TAB:      return DKEY_SELECT;
    default:            return 0;
    }
}

// Game-correct hardware bits for KEY_STATUS.status, matching
// AstroLander control.h D-Pad/button bit positions.
static u32 sdl_to_game_hw(SDL_Keycode sym) {
    switch (sym) {
    case SDLK_UP:       return 1u << 20;  // CONTROL_DPAD_UP
    case SDLK_DOWN:     return 1u << 27;  // CONTROL_DPAD_DOWN
    case SDLK_LEFT:     return 1u << 28;  // CONTROL_DPAD_LEFT
    case SDLK_RIGHT:    return 1u << 18;  // CONTROL_DPAD_RIGHT
    case SDLK_RETURN:   return 1u << 11;  // CONTROL_BUTTON_START
    case SDLK_TAB:      return 1u << 10;  // CONTROL_BUTTON_SELECT
    case SDLK_z:        return 1u << 31;  // CONTROL_BUTTON_A
    case SDLK_x:        return 1u << 21;  // CONTROL_BUTTON_B
    case SDLK_a:        return 1u << 16;  // CONTROL_BUTTON_X
    case SDLK_s:        return 1u << 6;   // CONTROL_BUTTON_Y
    case SDLK_q:        return 1u << 8;   // CONTROL_TRIGGER_LEFT
    case SDLK_w:        return 1u << 29;  // CONTROL_TRIGGER_RIGHT
    default:            return 0;
    }
}

void Display::present_blank() {
    if (m_initialized)
        SDL_RenderPresent(m_renderer);
}

void Display::save_screenshot(const char* path) {
    if (!m_initialized) return;

    SDL_Surface* dst = nullptr;
    if (m_argb_valid) {
        // flip_argb8888 path (32bpp ARGB8888 — normal Dingoo A320 game mode).
        // Read from m_argb_cache which is kept in sync with every flip_argb8888 call.
        // We cannot reliably use SDL_RenderReadPixels after SDL_RenderPresent because
        // double-buffered backends swap the back buffer, leaving it undefined.
        SDL_Surface* src = SDL_CreateRGBSurfaceWithFormatFrom(
            m_argb_cache, WIDTH, HEIGHT, 32, WIDTH * sizeof(u32), SDL_PIXELFORMAT_ARGB8888);
        if (!src) return;
        dst = SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_BGR24, 0);
        SDL_FreeSurface(src);
    } else {
        // flip / flip_strided path (16bpp RGB565).
        // Replicate the 5/6-bit component values into the vacated LSBs (e.g. 0x1F → 0xFF).
        SDL_Surface* src = SDL_CreateRGBSurfaceWithFormatFrom(
            m_framebuffer, WIDTH, HEIGHT, 16, WIDTH * PIXEL_SIZE, SDL_PIXELFORMAT_RGB565);
        if (!src) return;
        dst = SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_BGR24, 0);
        SDL_FreeSurface(src);
    }

    if (!dst) return;
    SDL_SaveBMP(dst, path);
    SDL_FreeSurface(dst);
    printf("[DISPLAY] Screenshot saved: %s\n", path);
}

u32 Display::bitmask_to_keycode(u32 bitmask) {
    switch (bitmask) {
        case DKEY_A:      return 0x01;
        case DKEY_B:      return 0x02;
        case DKEY_X:      return 0x03;
        case DKEY_Y:      return 0x04;
        case DKEY_L:      return 0x05;
        case DKEY_R:      return 0x06;
        case DKEY_START:  return 0x07;
        case DKEY_SELECT: return 0x08;
        case DKEY_UP:     return 0x09;
        case DKEY_DOWN:   return 0x0A;
        case DKEY_LEFT:   return 0x0B;
        case DKEY_RIGHT:  return 0x0C;
        default:          return 0;
    }
}

u32 Display::pop_input_event() {
    if (m_input_events.empty()) return 0;
    u32 ev = m_input_events.front();
    m_input_events.pop();
    return ev;
}

u32 Display::pop_key_event() {
    if (m_key_events.empty()) return 0;
    u32 ev = m_key_events.front();
    m_key_events.pop();
    return ev;
}

bool Display::pump_events() {
    static int screenshot_idx = 0;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) return true;
        if (event.type == SDL_KEYDOWN) {
            if (event.key.keysym.sym == SDLK_ESCAPE) return true;
            if (event.key.keysym.sym == SDLK_F12) {
                char path[64];
                snprintf(path, sizeof(path), "screenshot_%03d.bmp", screenshot_idx++);
                save_screenshot(path);
            }
            u32 dk = sdl_to_dingoo(event.key.keysym.sym);
            if (dk && !(m_dingoo_keys & dk) && m_input_events.size() < MAX_INPUT_EVENTS) {
                u32 code = bitmask_to_keycode(dk);
                if (code) {
                    u32 ev = (EVT_KEY_DOWN << 8) | code;
                    m_input_events.push(ev);
                    m_key_events.push(code);
                }
            }
            m_dingoo_keys |= dk;
            m_hw_keys |= sdl_to_game_hw(event.key.keysym.sym);
        }
        if (event.type == SDL_KEYUP) {
            u32 dk = sdl_to_dingoo(event.key.keysym.sym);
            if (dk && (m_dingoo_keys & dk) && m_input_events.size() < MAX_INPUT_EVENTS) {
                u32 code = bitmask_to_keycode(dk);
                if (code) {
                    u32 ev = (EVT_KEY_UP << 8) | code;
                    m_input_events.push(ev);
                    m_key_events.push(code);
                }
            }
            m_dingoo_keys &= ~dk;
            m_hw_keys &= ~sdl_to_game_hw(event.key.keysym.sym);
        }
    }
    return false;
}
