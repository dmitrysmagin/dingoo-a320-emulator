#include "display.h"
#include <cstdio>
#include <cstring>

Display::Display()
    : m_window(nullptr)
    , m_renderer(nullptr)
    , m_texture(nullptr)
    , m_frame_addr(0)
    , m_display_on(false)
    , m_dirty(false)
    , m_initialized(false)
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

void Display::flip() {
    if (!m_initialized) return;

    m_display_on = true;
    m_dirty = true;

    SDL_UpdateTexture(m_texture, nullptr, m_framebuffer, WIDTH * PIXEL_SIZE);
    SDL_RenderClear(m_renderer);
    SDL_RenderCopy(m_renderer, m_texture, nullptr, nullptr);
    SDL_RenderPresent(m_renderer);
}

bool Display::pump_events() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) return true;
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) return true;
    }
    return false;
}
