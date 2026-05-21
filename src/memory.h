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
    u32 vaddr_to_phys(u32 vaddr);

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
    bool is_got_address(u32 vaddr);
    int  got_index(u32 vaddr);

    // Get raw pointer (for syscall implementations that need direct access)
    u8* get_raw_ptr() { return m_mem.data(); }
    u32 size() const { return (u32)m_mem.size(); }

    // Set the code section range (writes to this range are rejected)
    void set_code_region(u32 phys_start, u32 size) {
        m_code_start = phys_start;
        m_code_end = phys_start + size;
    }

private:
    bool is_code_section(u32 phys) const {
        return m_code_start && phys >= m_code_start && phys < m_code_end;
    }

    std::vector<u8> m_mem;
    u32 m_hw_base;  // hardware register base (returns 0/ignores writes)
    u32 m_code_start = 0;
    u32 m_code_end = 0;
};

#endif // MEMORY_H
