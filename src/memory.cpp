#include "memory.h"
#include "cop0.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_set>

// Log unmapped accesses once per (vaddr, kind) so the log isn't flooded.
// kind: 0=read, 1=write
// When phys==0xFFFFFFFF the address was KUSEG/invalid — log the original vaddr too.
extern u32 g_cpu_pc;
static void log_unmapped(u32 phys, u32 val, int width, int kind, u32 vaddr = 0) {
    static std::unordered_set<u64> seen;
    u32 key_addr = (vaddr != 0) ? vaddr : phys;
    u64 key = ((u64)kind << 40) | ((u64)width << 32) | (u64)key_addr;
    if (!seen.insert(key).second) return;
    const char* region = "UNKNOWN";
    if      (phys == 0xFFFFFFFF)                          region = "KUSEG/invalid";
    else if (phys >= 0x10000000 && phys <  0x10001000)   region = "CPM";
    else if (phys >= 0x10001000 && phys <  0x10002000)   region = "INTC";
    else if (phys >= 0x10002000 && phys <  0x10003000)   region = "TCU/WDT";
    else if (phys >= 0x10003000 && phys <  0x10004000)   region = "RTC";
    else if (phys >= 0x10010000 && phys <  0x10011000)   region = "GPIO";
    else if (phys >= 0x10020000 && phys <  0x10021000)   region = "AIC";
    else if (phys >= 0x10021000 && phys <  0x10022000)   region = "MSC";
    else if (phys >= 0x10030000 && phys <  0x10031000)   region = "UART0";
    else if (phys >= 0x10043000 && phys <  0x10044000)   region = "SSI";
    else if (phys >= 0x10070000 && phys <  0x10071000)   region = "SADC";
    else if (phys >= 0x13010000 && phys <  0x13011000)   region = "EMC";
    else if (phys >= 0x13020000 && phys <  0x13021000)   region = "DMAC";
    else if (phys >= 0x13030000 && phys <  0x13031000)   region = "UHC";
    else if (phys >= 0x13040000 && phys <  0x13041000)   region = "UDC";
    else if (phys >= 0x13100000 && phys <  0x13101000)   region = "ETH";
    else if (phys >= 0x16000000 && phys <  0x16004000)   region = "TCSM";
    else if (phys >= 0x1FC00000 && phys <  0x1FC02000)   region = "BootROM";
    if (phys == 0xFFFFFFFF)
        printf("[HW-UNMAPPED] %s vaddr=0x%08X (%s) width=%d PC=0x%08X\n",
               kind ? "WRITE" : "READ", vaddr, region, width, g_cpu_pc);
    else
        printf("[HW-UNMAPPED] %s phys=0x%08X (%s) width=%d val=0x%08X PC=0x%08X\n",
               kind ? "WRITE" : "READ", phys, region, width, val, g_cpu_pc);
}

Memory::Memory()
    : m_mem(RAM_SIZE, 0)
    , m_hw_base(0x02000000)
    , m_write_counts(RAM_SIZE / 4096, 0)
    , m_tlb_exception(false)
    , m_tlb_exception_code(0)
    , m_tlb_exception_vaddr(0)
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

bool Memory::load_from_file(const std::string& path, u32 file_offset, u32 phys_addr, u32 size) {
    if (phys_addr + size > m_mem.size()) {
        fprintf(stderr, "[MEM] load_from_file out of bounds: phys=0x%08X size=0x%X mem_size=0x%X\n",
                phys_addr, size, (u32)m_mem.size());
        return false;
    }
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        fprintf(stderr, "[MEM] Cannot open %s\n", path.c_str());
        return false;
    }
    fseek(f, file_offset, SEEK_SET);
    size_t read_bytes = fread(&m_mem[phys_addr], 1, size, f);
    fclose(f);
    if (read_bytes != size) {
        fprintf(stderr, "[MEM] load_from_file short read: %zu/%u bytes\n", read_bytes, size);
        return false;
    }
    printf("[MEM] Loaded %u bytes from file+0x%X at phys 0x%08X\n", size, file_offset, phys_addr);
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
    // KSEG0/KSEG1 are always mapped
    if ((vaddr & KSEG_MASK) == KSEG0_BASE || (vaddr & KSEG_MASK) == KSEG1_BASE) {
        return (vaddr & KSEG0_KSEG1_MASK) < m_mem.size();
    }
    // KUSEG/KSEG2/3: probe TLB
    if (m_cop0) {
        auto result = m_cop0->tlb_translate(vaddr, false);
        if (result.hit) return result.phys < m_mem.size();
    }
    // Fallback identity for backward compat
    return vaddr < m_mem.size();
}

u32 Memory::vaddr_to_phys(u32 vaddr, bool write) {
    m_tlb_exception = false;
    m_tlb_exception_code = 0;
    m_tlb_exception_vaddr = 0;

    // KSEG0/KSEG1: direct-map (bypass TLB)
    if ((vaddr & KSEG_MASK) == KSEG0_BASE || (vaddr & KSEG_MASK) == KSEG1_BASE) {
        return vaddr & KSEG0_KSEG1_MASK;
    }
    // KUSEG (< 0x80000000) or KSEG2/3 (>= 0xC0000000): probe TLB
    if (m_cop0) {
        auto result = m_cop0->tlb_translate(vaddr, write);
        if (result.hit) {
            return result.phys;
        }
        // TLB miss — record exception state but fall through to identity fallback
        m_tlb_exception = true;
        m_tlb_exception_code = result.exception_code;
        m_tlb_exception_vaddr = vaddr;
    }
    // Identity fallback for KUSEG DRAM (backward compat for addresses not in TLB)
    if (vaddr < m_mem.size()) {
        return vaddr;
    }
    return 0xFFFFFFFF;
}

u8 Memory::read_u8(u32 vaddr) {
    u32 phys = vaddr_to_phys(vaddr, false);
    if (phys == 0xFFFFFFFF || phys >= m_mem.size()) {
        if (phys >= LCD_PAL_BASE && phys < LCD_PAL_BASE + LCD_PAL_SIZE) {
            u32 idx = (phys - LCD_PAL_BASE) / 2;
            u32 byte_off = (phys - LCD_PAL_BASE) & 1;
            return byte_off ? (m_lcd_palette[idx] >> 8) : (u8)(m_lcd_palette[idx] & 0xFF);
        }
        if (!m_tlb_exception) log_unmapped(phys, 0, 8, 0, vaddr);
        return 0;
    }
    return m_mem[phys];
}

u16 Memory::read_u16(u32 vaddr) {
    u32 phys = vaddr_to_phys(vaddr, false);
    if (phys == 0xFFFFFFFF || phys + 1 >= m_mem.size()) {
        if (phys >= LCD_PAL_BASE && phys < LCD_PAL_BASE + LCD_PAL_SIZE) {
            u32 idx = (phys - LCD_PAL_BASE) / 2;
            return m_lcd_palette[idx];
        }
        if (!m_tlb_exception) log_unmapped(phys, 0, 16, 0, vaddr);
        return 0;
    }
    return (u16)m_mem[phys] | ((u16)m_mem[phys + 1] << 8);
}

u32 Memory::read_u32(u32 vaddr) {
    u32 phys = vaddr_to_phys(vaddr, false);
    if (phys == 0xFFFFFFFF || phys + 3 >= m_mem.size()) {
        if (phys >= LCD_PAL_BASE && phys < LCD_PAL_BASE + LCD_PAL_SIZE) {
            u32 base = (phys - LCD_PAL_BASE) / 2;
            u32 lo = m_lcd_palette[base];
            u32 hi = (base + 1 < 256) ? m_lcd_palette[base + 1] : 0;
            return lo | (hi << 16);
        }
        if (!m_tlb_exception) log_unmapped(phys, 0, 32, 0, vaddr);
        return 0;
    }
    return (u32)m_mem[phys] | ((u32)m_mem[phys + 1] << 8) |
           ((u32)m_mem[phys + 2] << 16) | ((u32)m_mem[phys + 3] << 24);
}

// JZ4740 LCD controller register space: physical 0x13050000–0x130503FF
// 0x000–0x0FF: control regs, 0x200–0x3FF: palette RAM (256 × u16)
static void log_lcd_write(u32 phys, u32 val, int width) {
    u32 off = phys - 0x13050000;
    if (off >= 0x200 && off < 0x400) {
        u32 idx = (off - 0x200) / 2;
        printf("[LCD_PAL] write%d phys=0x%08X palette[%u] = 0x%08X\n", width, phys, idx, val);
        return;
    }
    static const struct { u32 off; const char* name; } regs[] = {
        {0x00, "LCD_CFG"},   {0x04, "LCD_CTRL"},  {0x08, "LCD_HSYNC"},
        {0x0C, "LCD_CFG2"},  {0x10, "LCD_VAT"},   {0x14, "LCD_DAV"},
        {0x18, "LCD_PS"},    {0x1C, "LCD_CLS"},   {0x20, "LCD_SPL"},
        {0x24, "LCD_REV"},   {0x30, "LCD_DAH"},   {0x34, "LCD_STATE"},
        {0x40, "LCD_DBA"},   {0x44, "LCD_SA0"},   {0x48, "LCD_FID0"},
        {0x4C, "LCD_CMD0"},  {0x50, "LCD_DBB"},   {0x54, "LCD_SA1"},
        {0x58, "LCD_FID1"},  {0x5C, "LCD_CMD1"},
    };
    const char* name = "LCD_???";
    for (auto& r : regs) if (r.off == off) { name = r.name; break; }
    printf("[LCD_REG] write%d phys=0x%08X %-12s = 0x%08X\n", width, phys, name, val);
}

// JZ4740 DMA controller register space: physical 0x10042000–0x100420FF
static void log_dma_write(u32 phys, u32 val, int width) {
    static const struct { u32 off; const char* name; } regs[] = {
        {0x00, "DMA_CTRL"},   {0x04, "DMA_IRQ"},     {0x08, "DMA_ADDR"},
        {0x0C, "DMA_CMD"},    {0x10, "DMA_SA"},      {0x14, "DMA_DA"},
        {0x18, "DMA_COUNT"},  {0x1C, "DMA_SKIP"},    {0x20, "DMA_STRIDE"},
        {0x24, "DMA_MODE"},   {0x28, "DMA_DESC_SA"}, {0x2C, "DMA_STATUS"},
    };
    u32 off = phys - 0x10042000;
    const char* name = "DMA_???";
    for (auto& r : regs) if (r.off == off) { name = r.name; break; }
    printf("[DMA] write%d phys=0x%08X %-12s = 0x%08X\n", width, phys, name, val);
}

// JZ4740 IPU register space: physical 0x13080000–0x130800FF
static void log_ipu_write(u32 phys, u32 val, int width) {
    static const struct { u32 off; const char* name; } regs[] = {
        {0x00, "IPU_CTRL"}, {0x04, "IPU_STATUS"}, {0x08, "IPU_D_FMT"},
        {0x0C, "IPU_IN_FM_GS"}, {0x10, "IPU_IN_SUBM"}, {0x14, "IPU_OUT_FM_GS"},
        {0x18, "IPU_RSZ_COEF_LUT"}, {0x1C, "IPU_CSC_C0_COEF"}, {0x20, "IPU_CSC_C1_COEF"},
        {0x24, "IPU_Y_ADDR"}, {0x28, "IPU_U_ADDR"}, {0x2C, "IPU_V_ADDR"},
        {0x30, "IPU_OUT_ADDR"}, {0x34, "IPU_IN_STRIDE"}, {0x38, "IPU_OUT_STRIDE"},
        {0x3C, "IPU_CSC_OFFSET_PARA"},
    };
    u32 off = phys - 0x13080000;
    const char* name = "IPU_???";
    for (auto& r : regs) if (r.off == off) { name = r.name; break; }
    printf("[IPU] write%d phys=0x%08X %-20s = 0x%08X\n", width, phys, name, val);
}

void Memory::write_u8(u32 vaddr, u8 val) {
    u32 phys = vaddr_to_phys(vaddr, true);
    if (phys == 0xFFFFFFFF || phys >= m_mem.size()) {
        if (!m_tlb_exception) {
            if (phys >= LCD_PAL_BASE && phys < LCD_PAL_BASE + LCD_PAL_SIZE) {
                u32 idx = (phys - LCD_PAL_BASE) / 2;
                u32 byte_off = (phys - LCD_PAL_BASE) & 1;
                u16 old = m_lcd_palette[idx];
                if (byte_off == 0) m_lcd_palette[idx] = (old & 0xFF00) | val;
                else               m_lcd_palette[idx] = (old & 0x00FF) | ((u16)val << 8);
                m_lcd_pal_dirty = true;
                return;
            }
            if      (phys >= 0x13050000 && phys < 0x13050400) log_lcd_write(phys, val, 8);
            else if (phys >= 0x10042000 && phys < 0x10042100) log_dma_write(phys, val, 8);
            else if (phys >= 0x13080000 && phys < 0x13080100) log_ipu_write(phys, val, 8);
            else log_unmapped(phys, val, 8, 1, vaddr);
        }
        return;
    }
    if (is_code_section(phys)) return;
    m_mem[phys] = val;
    m_write_counts[phys >> 12]++;
}

void Memory::write_u16(u32 vaddr, u16 val) {
    u32 phys = vaddr_to_phys(vaddr, true);
    if (phys == 0xFFFFFFFF || phys + 1 >= m_mem.size()) {
        if (!m_tlb_exception) {
            if (phys >= LCD_PAL_BASE && phys < LCD_PAL_BASE + LCD_PAL_SIZE) {
                u32 idx = (phys - LCD_PAL_BASE) / 2;
                m_lcd_palette[idx] = val;
                m_lcd_pal_dirty = true;
                return;
            }
            if      (phys >= 0x13050000 && phys < 0x13050400) log_lcd_write(phys, val, 16);
            else if (phys >= 0x10042000 && phys < 0x10042100) log_dma_write(phys, val, 16);
            else if (phys >= 0x13080000 && phys < 0x13080100) log_ipu_write(phys, val, 16);
            else log_unmapped(phys, val, 16, 1, vaddr);
        }
        return;
    }
    if (is_code_section(phys)) return;
    m_mem[phys]     = (u8)(val & 0xFF);
    m_mem[phys + 1] = (u8)((val >> 8) & 0xFF);
    m_write_counts[phys >> 12]++;
}

void Memory::write_u32(u32 vaddr, u32 val) {
    u32 phys = vaddr_to_phys(vaddr, true);
    if (phys == 0xFFFFFFFF || phys + 3 >= m_mem.size()) {
        if (!m_tlb_exception) {
            if (phys >= LCD_PAL_BASE && phys < LCD_PAL_BASE + LCD_PAL_SIZE) {
                u32 base = (phys - LCD_PAL_BASE) / 2;
                m_lcd_palette[base + 0] = (u16)(val & 0xFFFF);
                if (base + 1 < 256) m_lcd_palette[base + 1] = (u16)(val >> 16);
                m_lcd_pal_dirty = true;
                return;
            }
            if      (phys >= 0x13050000 && phys < 0x13050400) log_lcd_write(phys, val, 32);
            else if (phys >= 0x10042000 && phys < 0x10042100) log_dma_write(phys, val, 32);
            else if (phys >= 0x13080000 && phys < 0x13080100) log_ipu_write(phys, val, 32);
            else log_unmapped(phys, val, 32, 1, vaddr);
        }
        return;
    }
    if (is_code_section(phys)) return;
    m_mem[phys]     = (u8)(val & 0xFF);
    m_mem[phys + 1] = (u8)((val >> 8) & 0xFF);
    m_mem[phys + 2] = (u8)((val >> 16) & 0xFF);
    m_mem[phys + 3] = (u8)((val >> 24) & 0xFF);
    m_write_counts[phys >> 12]++;
}

std::string Memory::read_string(u32 vaddr, size_t max_len) {
    u32 phys = vaddr_to_phys(vaddr, false);
    if (phys == 0xFFFFFFFF || phys >= m_mem.size()) {
        return "<invalid>";
    }
    std::string s;
    for (size_t i = 0; i < max_len && (phys + i) < m_mem.size(); i++) {
        char c = (char)m_mem[phys + i];
        if (c == '\0') break;
        s += c;
    }
    return s;
}

void Memory::read_block(u32 vaddr, u8* dest, u32 size) {
    u32 phys = vaddr_to_phys(vaddr, false);
    if (phys == 0xFFFFFFFF || phys + size > m_mem.size()) {
        memset(dest, 0, size);
        return;
    }
    memcpy(dest, &m_mem[phys], size);
}

void Memory::write_block(u32 vaddr, const u8* src, u32 size) {
    u32 phys = vaddr_to_phys(vaddr, true);
    if (phys == 0xFFFFFFFF || phys + size > m_mem.size()) {
        // Check if this is a block write to the LCD palette
        if (phys >= LCD_PAL_BASE && phys < LCD_PAL_BASE + LCD_PAL_SIZE && size <= LCD_PAL_SIZE) {
            u32 start = (phys - LCD_PAL_BASE) / 2;
            u32 count = size / 2;
            for (u32 i = 0; i < count && start + i < 256; i++)
                m_lcd_palette[start + i] = ((u16)src[i*2]) | ((u16)src[i*2+1] << 8);
            m_lcd_pal_dirty = true;
            return;
        }
        log_unmapped(phys, 0, 32, 1, vaddr);
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
