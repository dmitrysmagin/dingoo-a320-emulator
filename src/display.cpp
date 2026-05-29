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
        fprintf(stderr, "[SDL] Failed to create renderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(m_window);
        SDL_Quit();
        return false;
    }

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
        if (phys + WIDTH * HEIGHT * PIXEL_SIZE <= ram_size) {
            memcpy(m_framebuffer, &guest_ram[phys], WIDTH * HEIGHT * PIXEL_SIZE);
            // Dump frame buffer periodically
            static u32 dump_count = 0;
            dump_count++;
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
    // Convert RGB565 framebuffer to RGBA8888 for SDL_SaveBMP
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, WIDTH, HEIGHT, 32, SDL_PIXELFORMAT_RGBA8888);
    if (!surf) return;
    u32* px = (u32*)surf->pixels;
    for (int i = 0; i < WIDTH * HEIGHT; i++) {
        u16 c = m_framebuffer[i];
        u8 r = ((c >> 11) & 0x1F) << 3;
        u8 g = ((c >>  5) & 0x3F) << 2;
        u8 b = ((c >>  0) & 0x1F) << 3;
        px[i] = SDL_MapRGBA(surf->format, r, g, b, 255);
    }
    SDL_SaveBMP(surf, path);
    SDL_FreeSurface(surf);
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
