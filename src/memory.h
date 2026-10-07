#ifndef MEMORY_H
#define MEMORY_H

#include "types.h"
#include <vector>
#include <string>

class Memory {
public:
    Memory();

    // Load binary data at physical address
    bool load_raw(const std::vector<u8>& data, u32 phys_addr);

    // Load data from file at physical address
    bool load_from_file(const std::string& path, u32 file_offset, u32 phys_addr, u32 size);

    // Zero a region (BSS)
    void zero_region(u32 phys_addr, u32 size);

    // Address translation
    bool is_mapped(u32 vaddr);
    u32 vaddr_to_phys(u32 vaddr, bool write = false);

    // Memory access
    u8   read_u8(u32 vaddr);
    u16  read_u16(u32 vaddr);
    u32  read_u32(u32 vaddr);
    void write_u8(u32 vaddr, u8 val);
    void write_u16(u32 vaddr, u16 val);
    void write_u32(u32 vaddr, u32 val);

    // String read (null-terminated, max 256 chars)
    std::string read_string(u32 vaddr, size_t max_len = 256);

    // Block copy
    void read_block(u32 vaddr, u8* dest, u32 size);
    void write_block(u32 vaddr, const u8* src, u32 size);

    // Check if address is in GOT trampoline region
    void set_got_range(u32 base, u32 count) { m_got_base = base; m_got_count = count; }
    bool is_got_address(u32 vaddr);
    int  got_index(u32 vaddr);

    // Get raw pointer (for syscall implementations that need direct access)
    u8* get_raw_ptr() { return m_mem.data(); }
    u32 size() const { return (u32)m_mem.size(); }

    // Phase 5 JIT accessors (read-only views, no behavior change):
    // code-section range for the JIT store fast path (unset in production),
    // and the write-hotspot base for inline write_counts bumps.
    u32 code_start() const { return m_code_start; }
    u32 code_end() const { return m_code_end; }
    u32* write_counts_base() { return m_write_counts.data(); }

    // Write hotspot tracking: counts writes per 4KB page
    void reset_write_counts() { std::fill(m_write_counts.begin(), m_write_counts.end(), 0); }
    u32  write_count(u32 page_4k) const {
        return page_4k < m_write_counts.size() ? m_write_counts[page_4k] : 0;
    }
    u32  page_count() const { return (u32)m_write_counts.size(); }

    // LCD palette: 256 × u16 RGB565 at phys 0x13050200–0x130503FF
    static constexpr u32 LCD_PAL_BASE = 0x13050200;
    static constexpr u32 LCD_PAL_SIZE = 512;
    u16* get_lcd_palette() { return m_lcd_palette; }
    const u16* get_lcd_palette() const { return m_lcd_palette; }
    bool lcd_palette_dirty() const { return m_lcd_pal_dirty; }
    void clear_lcd_palette_dirty() { m_lcd_pal_dirty = false; }

    // Set the code section range (writes to this range are rejected)
    void set_code_region(u32 phys_start, u32 size) {
        m_code_start = phys_start;
        m_code_end = phys_start + size;
    }

    // Write tracing: track writes in a specific phys range (for finding conversion loops)
    void set_trace_range(u32 phys_start, u32 phys_end) {
        m_trace_start = phys_start;
        m_trace_end = phys_end;
    }
    void clear_trace_range() { m_trace_start = m_trace_end = 0; }
    u32  trace_writes() const { return m_trace_write_count; }
    void reset_trace_writes() { m_trace_write_count = 0; m_trace_nonzero = 0; }

private:
    bool is_code_section(u32 phys) const {
        return m_code_start && phys >= m_code_start && phys < m_code_end;
    }

    u32 m_got_base = 0;
    u32 m_got_count = 72;
    std::vector<u8> m_mem;
    u32 m_hw_base;
    u32 m_code_start = 0;
    u32 m_code_end = 0;
    std::vector<u32> m_write_counts;  // one counter per 4KB page
    u32 m_trace_start = 0;
    u32  m_trace_end = 0;
    u32  m_trace_write_count = 0;
    u32  m_trace_nonzero = 0;
    u16  m_lcd_palette[256] = {};
    bool m_lcd_pal_dirty = false;
};

#endif // MEMORY_H
