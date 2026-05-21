#include "memory.h"
#include <cstdio>
#include <cstring>

Memory::Memory()
    : m_mem(RAM_SIZE, 0)
    , m_hw_base(0x02000000)  // above our RAM, reads return 0
{
}

bool Memory::load_raw(const std::vector<u8>& data, u32 phys_addr) {
    if (phys_addr + data.size() > m_mem.size()) {
        fprintf(stderr, "[MEM] RAWD too large: phys=0x%08X size=0x%X mem_size=0x%X\n",
                phys_addr, (u32)data.size(), (u32)m_mem.size());
        return false;
    }
    memcpy(&m_mem[phys_addr], data.data(), data.size());
    printf("[MEM] Loaded %u bytes at phys 0x%08X\n", (u32)data.size(), phys_addr);
    return true;
}

void Memory::zero_region(u32 phys_addr, u32 size) {
    if (phys_addr + size > m_mem.size()) {
        fprintf(stderr, "[MEM] zero_region out of bounds: 0x%08X + 0x%X\n", phys_addr, size);
        return;
    }
    memset(&m_mem[phys_addr], 0, size);
}

bool Memory::is_mapped(u32 vaddr) {
    // Only KSEG0/KSEG1 are mapped
    if ((vaddr & KSEG_MASK) != KSEG0_BASE && (vaddr & KSEG_MASK) != KSEG1_BASE) {
        return false;
    }
    u32 phys = vaddr & KSEG0_KSEG1_MASK;
    return phys < m_mem.size();
}

u32 Memory::vaddr_to_phys(u32 vaddr) {
    if ((vaddr & KSEG_MASK) == KSEG0_BASE || (vaddr & KSEG_MASK) == KSEG1_BASE) {
        return vaddr & KSEG0_KSEG1_MASK;
    }
    // Direct physical address within our RAM
    if (vaddr < m_mem.size()) {
        return vaddr;
    }
    return 0xFFFFFFFF;  // unmapped
}

u8 Memory::read_u8(u32 vaddr) {
    u32 phys = vaddr_to_phys(vaddr);
    if (phys == 0xFFFFFFFF || phys >= m_mem.size()) {
        // Hardware register read → return 0
        return 0;
    }
    return m_mem[phys];
}

u16 Memory::read_u16(u32 vaddr) {
    u32 phys = vaddr_to_phys(vaddr);
    if (phys == 0xFFFFFFFF || phys + 1 >= m_mem.size()) {
        return 0;
    }
    return (u16)m_mem[phys] | ((u16)m_mem[phys + 1] << 8);
}

u32 Memory::read_u32(u32 vaddr) {
    u32 phys = vaddr_to_phys(vaddr);
    if (phys == 0xFFFFFFFF || phys + 3 >= m_mem.size()) {
        return 0;
    }
    return (u32)m_mem[phys] | ((u32)m_mem[phys + 1] << 8) |
           ((u32)m_mem[phys + 2] << 16) | ((u32)m_mem[phys + 3] << 24);
}

void Memory::write_u8(u32 vaddr, u8 val) {
    u32 phys = vaddr_to_phys(vaddr);
    if (phys == 0xFFFFFFFF || phys >= m_mem.size()) {
        // Hardware register write → ignore
        return;
    }
    m_mem[phys] = val;
}

void Memory::write_u16(u32 vaddr, u16 val) {
    u32 phys = vaddr_to_phys(vaddr);
    if (phys == 0xFFFFFFFF || phys + 1 >= m_mem.size()) {
        return;
    }
    m_mem[phys]     = (u8)(val & 0xFF);
    m_mem[phys + 1] = (u8)((val >> 8) & 0xFF);
}

void Memory::write_u32(u32 vaddr, u32 val) {
    u32 phys = vaddr_to_phys(vaddr);
    if (phys == 0xFFFFFFFF || phys + 3 >= m_mem.size()) {
        return;
    }
    m_mem[phys]     = (u8)(val & 0xFF);
    m_mem[phys + 1] = (u8)((val >> 8) & 0xFF);
    m_mem[phys + 2] = (u8)((val >> 16) & 0xFF);
    m_mem[phys + 3] = (u8)((val >> 24) & 0xFF);
}

std::string Memory::read_string(u32 vaddr, size_t max_len) {
    std::string result;
    u32 phys = vaddr_to_phys(vaddr);
    if (phys == 0xFFFFFFFF || phys >= m_mem.size()) {
        return "<invalid>";
    }
    for (size_t i = 0; i < max_len && (phys + i) < m_mem.size(); i++) {
        char c = (char)m_mem[phys + i];
        if (c == '\0') break;
        result += c;
    }
    return result;
}

void Memory::read_block(u32 vaddr, u8* dest, u32 size) {
    u32 phys = vaddr_to_phys(vaddr);
    if (phys == 0xFFFFFFFF || phys + size > m_mem.size()) {
        memset(dest, 0, size);
        return;
    }
    memcpy(dest, &m_mem[phys], size);
}

void Memory::write_block(u32 vaddr, const u8* src, u32 size) {
    u32 phys = vaddr_to_phys(vaddr);
    if (phys == 0xFFFFFFFF || phys + size > m_mem.size()) {
        return;
    }
    memcpy(&m_mem[phys], src, size);
}

bool Memory::is_got_address(u32 vaddr) {
    return vaddr >= GOT_BASE && vaddr < GOT_BASE + (GOT_COUNT * GOT_ENTRY_SIZE);
}

int Memory::got_index(u32 vaddr) {
    if (!is_got_address(vaddr)) return -1;
    return (int)((vaddr - GOT_BASE) / GOT_ENTRY_SIZE);
}
