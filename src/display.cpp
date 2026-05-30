#include "display.h"
#include <cstdio>
#include <cstring>
#include <cstdint>

Display::Display()
    : m_window(nullptr)
    , m_renderer(nullptr)
    , m_texture(nullptr)
    , m_frame_addr(0)
    , m_display_on(true)
    , m_dirty(false)
    , m_initialized(false)
    , m_dingoo_keys(0)
{
    memset(m_framebuffer, 0, sizeof(m_framebuffer));
}

Display::~Display() {
    shutdown();
}

bool Display::init() {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
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

    // Dingoo A320 (JZ4740, little-endian MIPS) uses RGB565: u16 with R at bits[15:11],
    // G at bits[10:5], B at bits[4:0], stored little-endian.  SDL_PIXELFORMAT_RGB565 on
    // a little-endian x86/x64 host uses the identical packed u16 layout, so guest framebuffer
    // bytes upload to the texture without any byte-swap or channel conversion.
    m_texture = SDL_CreateTexture(
        m_renderer,
        SDL_PIXELFORMAT_RGB565,
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

    // Confirm the texture format SDL actually allocated (renderer may substitute)
    {
        Uint32 fmt; int access, tw, th;
        SDL_QueryTexture(m_texture, &fmt, &access, &tw, &th);
        if (fmt != SDL_PIXELFORMAT_RGB565)
            printf("[DISPLAY] Note: texture format substituted to 0x%08X (SDL_PIXELFORMAT_RGB565=0x%08X)\n",
                   fmt, SDL_PIXELFORMAT_RGB565);
    }

    m_initialized = true;
    printf("[DISPLAY] SDL2 initialized: %dx%d (scale %d)\n", WIDTH * SCALE, HEIGHT * SCALE, SCALE);
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

    // Read frame buffer data from guest RAM if available
    if (guest_ram && m_frame_addr + WIDTH * HEIGHT * PIXEL_SIZE <= ram_size) {
        u32 phys = m_frame_addr & 0x1FFFFFFF;
        if (phys + WIDTH * HEIGHT * PIXEL_SIZE <= ram_size)
            memcpy(m_framebuffer, &guest_ram[phys], WIDTH * HEIGHT * PIXEL_SIZE);
    }

    SDL_UpdateTexture(m_texture, nullptr, m_framebuffer, WIDTH * PIXEL_SIZE);
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

    SDL_UpdateTexture(m_texture, nullptr, m_framebuffer, WIDTH * PIXEL_SIZE);
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

void Display::present_blank() {
    if (m_initialized)
        SDL_RenderPresent(m_renderer);
}

void Display::save_screenshot(const char* path) {
    if (!m_initialized) return;
    // Wrap m_framebuffer as an RGB565 surface (zero-copy), then let SDL convert to
    // BGR24 for the BMP.  SDL's converter replicates the top bits into the vacated LSBs
    // (e.g. 5-bit 0x1F → 8-bit 0xFF) which a plain left-shift would not do.
    SDL_Surface* src = SDL_CreateRGBSurfaceWithFormatFrom(
        m_framebuffer, WIDTH, HEIGHT, 16, WIDTH * PIXEL_SIZE, SDL_PIXELFORMAT_RGB565);
    if (!src) return;
    SDL_Surface* dst = SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_BGR24, 0);
    SDL_FreeSurface(src);
    if (!dst) return;
    SDL_SaveBMP(dst, path);
    SDL_FreeSurface(dst);
    printf("[DISPLAY] Screenshot saved: %s\n", path);
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
            m_dingoo_keys |= sdl_to_dingoo(event.key.keysym.sym);
        }
        if (event.type == SDL_KEYUP) {
            m_dingoo_keys &= ~sdl_to_dingoo(event.key.keysym.sym);
        }
    }
    return false;
}
