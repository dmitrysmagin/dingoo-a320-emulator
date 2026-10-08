#include "display.h"
#include "log.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

Display::Display()
    : m_window(nullptr)
    , m_renderer(nullptr)
    , m_texture(nullptr)
    , m_framebuffer(std::make_unique<u16[]>(WIDTH * HEIGHT))
    , m_argb_cache(std::make_unique<u32[]>(WIDTH * HEIGHT))
    , m_frame_addr(0)
    , m_frame_back(0)
    , m_display_on(true)
    , m_dirty(false)
    , m_initialized(false)
    , m_argb_valid(false)
    , m_dingoo_keys(0)
    , m_hw_keys(0)
    , m_prev_dingoo_keys(0)
    , m_rotate(0)
{
    memset(m_framebuffer.get(), 0, WIDTH * HEIGHT * sizeof(u16));
    memset(m_argb_cache.get(),  0, WIDTH * HEIGHT * sizeof(u32));
}

Display::~Display() {
    shutdown();
}

bool Display::set_rotate(int degrees) {
    if (degrees == 270)
        degrees = -90;
    if (degrees != 0 && degrees != 90 && degrees != -90)
        return false;
    m_rotate = degrees;
    return true;
}

bool Display::init() {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "[SDL] Failed to init: %s\n", SDL_GetError());
        return false;
    }

    const int win_w = (m_rotate == 0) ? WIDTH * SCALE : HEIGHT * SCALE;
    const int win_h = (m_rotate == 0) ? HEIGHT * SCALE : WIDTH * SCALE;

    m_window = SDL_CreateWindow(
        "7days - Dingoo A320 Emulator",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        win_w, win_h,
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
    if (m_rotate)
        log_dbg("[DISPLAY] SDL2 initialized: %dx%d (scale %d) rotate=%d",
                win_w, win_h, SCALE, m_rotate);
    else
        log_dbg("[DISPLAY] SDL2 initialized: %dx%d (scale %d)", win_w, win_h, SCALE);

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
        memcpy(m_framebuffer.get(), &guest_ram[phys], fb_bytes);

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
        SDL_UpdateTexture(m_texture, nullptr, m_argb_cache.get(), WIDTH * sizeof(u32));
    }
    // Present once per host vsync (present_blank) so a real D3D window is not
    // flipped from inside the guest LCD syscall — that path crashes some drivers.
}

void Display::flip_composite(const u8* guest_ram, u32 ram_size, u32 overlay_phys) {
    if (!m_initialized) return;

    m_display_on = true;
    m_dirty = true;

    constexpr u32 fb_bytes = WIDTH * HEIGHT * PIXEL_SIZE;
    u32 bg_phys = m_frame_addr & 0x1FFFFFFF;

    // Copy background layer
    if (guest_ram && bg_phys + fb_bytes <= ram_size)
        memcpy(m_framebuffer.get(), &guest_ram[bg_phys], fb_bytes);

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
// Does not Present — the host vsync path presents once per outer loop.
void Display::upload_and_present() {
    if (!m_initialized || !m_framebuffer || !m_argb_cache) return;
    for (int i = 0; i < WIDTH * HEIGHT; i++) {
        u16 px = m_framebuffer[i];
        u8 r5 = (px >> 11) & 0x1F;
        u8 g6 = (px >>  5) & 0x3F;
        u8 b5 = (px      ) & 0x1F;
        u8 r = (r5 << 3) | (r5 >> 2);
        u8 g = (g6 << 2) | (g6 >> 4);
        u8 b = (b5 << 3) | (b5 >> 2);
        m_argb_cache[i] = (0xFFu << 24) | ((u32)r << 16) | ((u32)g << 8) | b;
    }
    m_argb_valid = true;
    SDL_UpdateTexture(m_texture, nullptr, m_argb_cache.get(), WIDTH * sizeof(u32));
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

u32 Display::map_view_dpad(u32 dkey) const {
    if (m_rotate == 0)
        return dkey;
    // Arrow keys follow the rotated window: window-up is visual up.
    // 90 CW:  window up/down/left/right -> guest left/right/down/up
    // -90 CCW: window up/down/left/right -> guest right/left/up/down
    if (m_rotate == 90) {
        switch (dkey) {
        case DKEY_UP:    return DKEY_LEFT;
        case DKEY_DOWN:  return DKEY_RIGHT;
        case DKEY_LEFT:  return DKEY_DOWN;
        case DKEY_RIGHT: return DKEY_UP;
        default:         return dkey;
        }
    }
    switch (dkey) {
    case DKEY_UP:    return DKEY_RIGHT;
    case DKEY_DOWN:  return DKEY_LEFT;
    case DKEY_LEFT:  return DKEY_UP;
    case DKEY_RIGHT: return DKEY_DOWN;
    default:         return dkey;
    }
}

void Display::copy_texture() {
    if (!m_renderer || !m_texture) return;
    if (m_rotate == 0) {
        SDL_RenderCopy(m_renderer, m_texture, nullptr, nullptr);
        return;
    }

    int win_w = 0, win_h = 0;
    SDL_GetRendererOutputSize(m_renderer, &win_w, &win_h);
    if (win_w <= 0 || win_h <= 0)
        SDL_GetWindowSize(m_window, &win_w, &win_h);

    // After ±90°, the presented image is HEIGHT × WIDTH. Fit that in the window,
    // then place the un-rotated dest rect so RenderCopyEx rotates about its center.
    const float fit_w = (float)win_w / (float)HEIGHT;
    const float fit_h = (float)win_h / (float)WIDTH;
    const float fit = (fit_w < fit_h) ? fit_w : fit_h;
    const int dst_w = (int)((float)WIDTH * fit + 0.5f);
    const int dst_h = (int)((float)HEIGHT * fit + 0.5f);
    SDL_Rect dst = {
        win_w / 2 - dst_w / 2,
        win_h / 2 - dst_h / 2,
        dst_w,
        dst_h
    };
    SDL_RenderCopyEx(m_renderer, m_texture, nullptr, &dst, (double)m_rotate, nullptr, SDL_FLIP_NONE);
}

void Display::present_blank() {
    if (!m_initialized || !m_renderer || !m_texture) return;
    // Must Copy before Present: a bare SDL_RenderPresent on D3D flips an
    // undefined back buffer and crashes some Windows drivers. Offscreen
    // software backends hide that.
    SDL_RenderClear(m_renderer);
    copy_texture();
    SDL_RenderPresent(m_renderer);
}

static SDL_Surface* snapshot_rgb24(bool argb_valid, u32* argb, u16* rgb565) {
    SDL_Surface* src;
    if (argb_valid) {
        // flip_argb8888 path (32bpp ARGB8888 — normal Dingoo A320 game mode).
        // Read from m_argb_cache which is kept in sync with every flip_argb8888 call.
        // We cannot reliably use SDL_RenderReadPixels after SDL_RenderPresent because
        // double-buffered backends swap the back buffer, leaving it undefined.
        src = SDL_CreateRGBSurfaceWithFormatFrom(
            argb, Display::WIDTH, Display::HEIGHT, 32,
            Display::WIDTH * (int)sizeof(u32), SDL_PIXELFORMAT_ARGB8888);
    } else {
        // flip / flip_strided path (16bpp RGB565).
        src = SDL_CreateRGBSurfaceWithFormatFrom(
            rgb565, Display::WIDTH, Display::HEIGHT, 16,
            Display::WIDTH * Display::PIXEL_SIZE, SDL_PIXELFORMAT_RGB565);
    }
    if (!src) return nullptr;
    SDL_Surface* dst = SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_RGB24, 0);
    SDL_FreeSurface(src);
    return dst;
}

void Display::save_screenshot(const char* path) {
    if (!m_initialized) return;

    SDL_Surface* rgb = snapshot_rgb24(m_argb_valid, m_argb_cache.get(), m_framebuffer.get());
    if (!rgb) return;
    SDL_Surface* dst = SDL_ConvertSurfaceFormat(rgb, SDL_PIXELFORMAT_BGR24, 0);
    SDL_FreeSurface(rgb);
    if (!dst) return;
    SDL_SaveBMP(dst, path);
    SDL_FreeSurface(dst);
    log_dbg("[DISPLAY] Screenshot saved: %s\n", path);
}

namespace {

u32 crc32_update(u32 crc, const u8* data, size_t len) {
    static u32 table[256];
    static bool ready = false;
    if (!ready) {
        for (u32 i = 0; i < 256; i++) {
            u32 c = i;
            for (int k = 0; k < 8; k++)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        ready = true;
    }
    for (size_t i = 0; i < len; i++)
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

void put_be32(FILE* f, u32 v) {
    u8 b[4] = { (u8)(v >> 24), (u8)(v >> 16), (u8)(v >> 8), (u8)v };
    fwrite(b, 1, 4, f);
}

void write_png_chunk(FILE* f, const char type[4], const u8* data, u32 len) {
    put_be32(f, len);
    fwrite(type, 1, 4, f);
    if (len && data)
        fwrite(data, 1, len, f);
    u32 crc = 0xFFFFFFFFu;
    crc = crc32_update(crc, reinterpret_cast<const u8*>(type), 4);
    if (len && data)
        crc = crc32_update(crc, data, len);
    put_be32(f, crc ^ 0xFFFFFFFFu);
}

u32 adler32(const u8* data, size_t len) {
    u32 a = 1, b = 0;
    while (len) {
        size_t n = len > 5552 ? 5552 : len;
        for (size_t i = 0; i < n; i++) {
            a += data[i];
            b += a;
        }
        a %= 65521;
        b %= 65521;
        data += n;
        len -= n;
    }
    return (b << 16) | a;
}

bool write_png_rgb24(const char* path, const u8* pixels, int width, int height, int pitch) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;

    static const u8 sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    fwrite(sig, 1, 8, f);

    u8 ihdr[13] = {};
    ihdr[0] = (u8)(width >> 24); ihdr[1] = (u8)(width >> 16);
    ihdr[2] = (u8)(width >> 8);  ihdr[3] = (u8)width;
    ihdr[4] = (u8)(height >> 24); ihdr[5] = (u8)(height >> 16);
    ihdr[6] = (u8)(height >> 8);  ihdr[7] = (u8)height;
    ihdr[8] = 8;   // bit depth
    ihdr[9] = 2;   // color type: truecolor RGB
    write_png_chunk(f, "IHDR", ihdr, 13);

    const size_t row_raw = 1 + (size_t)width * 3;
    std::vector<u8> raw((size_t)height * row_raw);
    for (int y = 0; y < height; y++) {
        u8* dst = raw.data() + (size_t)y * row_raw;
        dst[0] = 0; // filter None
        memcpy(dst + 1, pixels + (size_t)y * (size_t)pitch, (size_t)width * 3);
    }

    // zlib-wrapped uncompressed DEFLATE stored blocks (no extra library).
    const size_t raw_len = raw.size();
    const size_t nblocks = (raw_len + 65534) / 65535;
    std::vector<u8> zbuf(2 + nblocks * 5 + raw_len + 4);
    size_t z = 0;
    zbuf[z++] = 0x78;
    zbuf[z++] = 0x01;
    size_t off = 0;
    size_t remain = raw_len;
    while (remain) {
        size_t chunk = remain > 65535 ? 65535 : remain;
        bool last = (remain == chunk);
        zbuf[z++] = last ? 0x01 : 0x00;
        zbuf[z++] = (u8)(chunk & 0xFF);
        zbuf[z++] = (u8)(chunk >> 8);
        u16 nlen = (u16)~(u16)chunk;
        zbuf[z++] = (u8)(nlen & 0xFF);
        zbuf[z++] = (u8)(nlen >> 8);
        memcpy(zbuf.data() + z, raw.data() + off, chunk);
        z += chunk;
        off += chunk;
        remain -= chunk;
    }
    u32 adler = adler32(raw.data(), raw_len);
    zbuf[z++] = (u8)(adler >> 24);
    zbuf[z++] = (u8)(adler >> 16);
    zbuf[z++] = (u8)(adler >> 8);
    zbuf[z++] = (u8)adler;

    write_png_chunk(f, "IDAT", zbuf.data(), (u32)z);
    write_png_chunk(f, "IEND", nullptr, 0);

    bool ok = ferror(f) == 0;
    fclose(f);
    return ok;
}

} // namespace

void Display::save_screenshot_png(const char* path) {
    if (!m_initialized) return;

    SDL_Surface* rgb = snapshot_rgb24(m_argb_valid, m_argb_cache.get(), m_framebuffer.get());
    if (!rgb) return;

    SDL_LockSurface(rgb);
    bool ok = write_png_rgb24(path, static_cast<const u8*>(rgb->pixels),
                              rgb->w, rgb->h, rgb->pitch);
    SDL_UnlockSurface(rgb);
    SDL_FreeSurface(rgb);

    if (!ok)
        fprintf(stderr, "[DISPLAY] Failed to write PNG: %s\n", path);
    else
        log_dbg("[DISPLAY] Screenshot saved: %s (%dx%d)\n", path, WIDTH, HEIGHT);
}

void Display::set_game_name(const char* name) {
    m_game_name = (name && name[0]) ? name : "screenshot";
}

void Display::save_f12_screenshot() {
#ifdef _WIN32
    _mkdir("screenshots");
#else
    mkdir("screenshots", 0755);
#endif

    std::string stem = m_game_name.empty() ? "screenshot" : m_game_name;
    for (char& c : stem) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|')
            c = '_';
    }

    char path[512];
    int n = 1;
    for (;;) {
        snprintf(path, sizeof(path), "screenshots/%s%d.png", stem.c_str(), n);
        struct stat st;
        if (stat(path, &st) != 0)
            break;
        if (++n > 99999) {
            fprintf(stderr, "[DISPLAY] Screenshot: too many files in screenshots/\n");
            return;
        }
    }
    save_screenshot_png(path);
}

static u32 dkey_to_hw(u32 dk) {
    switch (dk) {
    case DKEY_UP:     return 1u << 20;
    case DKEY_DOWN:   return 1u << 27;
    case DKEY_LEFT:   return 1u << 28;
    case DKEY_RIGHT:  return 1u << 18;
    case DKEY_START:  return 1u << 11;
    case DKEY_SELECT: return 1u << 10;
    case DKEY_A:      return 1u << 31;
    case DKEY_B:      return 1u << 21;
    case DKEY_X:      return 1u << 16;
    case DKEY_Y:      return 1u << 6;
    case DKEY_L:      return 1u << 8;
    case DKEY_R:      return 1u << 29;
    default:          return 0;
    }
}

void Display::inject_dingoo_key(u32 dk, bool down) {
    if (!dk) return;
    u32 hw = dkey_to_hw(dk);
    u32 code = bitmask_to_keycode(dk);
    if (down) {
        if (!(m_dingoo_keys & dk) && code && m_input_events.size() < MAX_INPUT_EVENTS) {
            u32 ev = (EVT_KEY_DOWN << 8) | code;
            m_input_events.push(ev);
            m_key_events.push(ev);
        }
        m_dingoo_keys |= dk;
        m_hw_keys |= hw;
    } else {
        if ((m_dingoo_keys & dk) && code && m_input_events.size() < MAX_INPUT_EVENTS) {
            u32 ev = (EVT_KEY_UP << 8) | code;
            m_input_events.push(ev);
            m_key_events.push(ev);
        }
        m_dingoo_keys &= ~dk;
        m_hw_keys &= ~hw;
    }
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
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) return true;
        if (event.type == SDL_KEYDOWN) {
            if (event.key.keysym.sym == SDLK_ESCAPE) return true;
            if (event.key.keysym.sym == SDLK_F12) {
                if (!event.key.repeat)
                    save_f12_screenshot();
                continue;
            }
            u32 dk = map_view_dpad(sdl_to_dingoo(event.key.keysym.sym));
            if (dk && !(m_dingoo_keys & dk) && m_input_events.size() < MAX_INPUT_EVENTS) {
                u32 code = bitmask_to_keycode(dk);
                if (code) {
                    u32 ev = (EVT_KEY_DOWN << 8) | code;
                    m_input_events.push(ev);
                    m_key_events.push(ev);
                }
            }
            m_dingoo_keys |= dk;
            m_hw_keys |= dkey_to_hw(dk);
        }
        if (event.type == SDL_KEYUP) {
            u32 dk = map_view_dpad(sdl_to_dingoo(event.key.keysym.sym));
            if (dk && (m_dingoo_keys & dk) && m_input_events.size() < MAX_INPUT_EVENTS) {
                u32 code = bitmask_to_keycode(dk);
                if (code) {
                    u32 ev = (EVT_KEY_UP << 8) | code;
                    m_input_events.push(ev);
                    m_key_events.push(ev);
                }
            }
            m_dingoo_keys &= ~dk;
            m_hw_keys &= ~dkey_to_hw(dk);
        }
    }
    return false;
}
