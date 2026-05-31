#include "cop0.h"
#include <cstdio>
#include <cstring>

void COP0::reset() {
    memset(&regs, 0, sizeof(regs));
    memset(&m_tlb, 0, sizeof(m_tlb));
    regs.prid  = 0x00018200;  // MIPS 4Kc
    regs.config = 0x00008000; // K0=1 (cacheable)
    regs.status = 0x00000000; // interrupts disabled
    regs.cause  = 0x00000000;
    regs.random = 31;         // starts at top
}

u32 COP0::mfc0(int rd) {
    switch (rd) {
    case 0:  return regs.index;
    case 1:  return regs.random;
    case 2:  return regs.entry_lo0;
    case 3:  return regs.entry_lo1;
    case 4:  return regs.context;
    case 5:  return regs.page_mask;
    case 6:  return regs.wired;
    case 8:  return regs.bad_vaddr;
    case 9:  return regs.count;
    case 10: return regs.entry_hi;
    case 11: return regs.compare;
    case 12: return regs.status;
    case 13: return regs.cause;
    case 14: return regs.epc;
    case 15: return regs.prid;
    case 16: return regs.config;
    case 26: return regs.ecc;
    case 27: return regs.cache_err;
    case 28: return regs.tag_lo;
    case 30: return regs.err_epc;
    default:
        printf("[COP0] MFC0 unknown rd=%d\n", rd);
        return 0;
    }
}

void COP0::mtc0(int rd, u32 value) {
    switch (rd) {
    case 0:  regs.index = value; break;
    case 1:  /* Random is read-only */ break;
    case 2:  regs.entry_lo0 = value; break;
    case 3:  regs.entry_lo1 = value; break;
    case 4:  regs.context = value; break;
    case 5:  regs.page_mask = value; break;
    case 6:  regs.wired = value & 0x1F; break;
    case 8:  regs.bad_vaddr = value; break;
    case 9:  regs.count = value; break;
    case 10: regs.entry_hi = value & 0xFFFFE0FF; break; // VPN2 + ASID only
    case 11: regs.compare = value; break;
    case 12: regs.status = value; break;
    case 13: regs.cause = value; break;
    case 14: regs.epc = value; break;
    case 16: regs.config = value; break;
    default:
        printf("[COP0] MTC0 unknown rd=%d value=0x%08X\n", rd, value);
        break;
    }
}

// Decode page size from PageMask register
static void decode_page_mask(u32 page_mask, u32& page_size, u32& vpn_shift, u32& extra_bits) {
    u32 mask = (page_mask >> 13) & 0xFFFF;
    extra_bits = 0;
    if (mask) {
        u32 m = mask + 1;
        while (m > 1) { m >>= 1; extra_bits++; }
    }
    vpn_shift = 13 + extra_bits;
    page_size = (mask + 1) * 4096;
}

void COP0::tlbwi() {
    u32 idx = regs.index & 0x1F;
    m_tlb[idx].entry_hi   = regs.entry_hi & 0xFFFFE0FF;
    m_tlb[idx].entry_lo0  = regs.entry_lo0;
    m_tlb[idx].entry_lo1  = regs.entry_lo1;
    m_tlb[idx].page_mask  = regs.page_mask & 0x1FFFE000;
}

void COP0::tlbr() {
    u32 idx = regs.index & 0x1F;
    regs.entry_hi  = m_tlb[idx].entry_hi;
    regs.entry_lo0 = m_tlb[idx].entry_lo0;
    regs.entry_lo1 = m_tlb[idx].entry_lo1;
    regs.page_mask = m_tlb[idx].page_mask;
}

void COP0::tlbwr() {
    u32 wired = regs.wired & 0x1F;
    u32 upper = 31;
    u32 range = upper - wired + 1;
    u32 idx = wired + (regs.random % range);
    idx &= 0x1F;
    m_tlb[idx].entry_hi   = regs.entry_hi & 0xFFFFE0FF;
    m_tlb[idx].entry_lo0  = regs.entry_lo0;
    m_tlb[idx].entry_lo1  = regs.entry_lo1;
    m_tlb[idx].page_mask  = regs.page_mask & 0x1FFFE000;
}

void COP0::tlbp() {
    u32 probe_vpn2 = regs.entry_hi & 0xFFFFE000;
    u32 probe_asid = regs.entry_hi & 0xFF;

    for (int i = 0; i < 32; i++) {
        TLBEntry& e = m_tlb[i];
        // Skip empty entries (entry_hi == 0 doesn't necessarily mean empty,
        // but we check if at least one of entry_lo0/1 has V=1 or the entry was written)
        // Simplification: probe against all entries, empty ones won't match anyway

        u32 page_size, vpn_shift, extra_bits;
        decode_page_mask(e.page_mask, page_size, vpn_shift, extra_bits);

        u32 entry_vpn2 = e.entry_hi & 0xFFFFE000;
        u32 entry_asid = e.entry_hi & 0xFF;
        bool global = (e.entry_lo0 & 1) && (e.entry_lo1 & 1);

        bool vpn_match;
        if (extra_bits == 0) {
            vpn_match = (entry_vpn2 == probe_vpn2);
        } else {
            u32 shift = extra_bits;
            vpn_match = ((entry_vpn2 >> shift) == (probe_vpn2 >> shift));
        }

        if (vpn_match && (global || entry_asid == probe_asid)) {
            regs.index = i;
            return;
        }
    }
    regs.index = 0x80000000; // bit 31 = 1: not found
}

TLBResult COP0::tlb_translate(u32 vaddr, bool write) {
    TLBResult result;
    result.hit = false;
    result.phys = 0xFFFFFFFF;
    result.exception_code = 0;

    u32 current_asid = regs.entry_hi & 0xFF;

    for (int i = 0; i < 32; i++) {
        TLBEntry& e = m_tlb[i];

        u32 page_size, vpn_shift, extra_bits;
        decode_page_mask(e.page_mask, page_size, vpn_shift, extra_bits);

        u32 entry_vpn2 = (e.entry_hi & 0xFFFFE000) >> 13; // entry's VPN2 in 19-bit form
        // But vpn_shift may be > 13, so we need to compare at the right granularity
        // entry_vpn2 is 19 bits from EntryHi[31:13]
        // We compare vaddr >> vpn_shift with entry_vpn2 >> extra_bits

        bool vpn_match;
        if (extra_bits == 0) {
            vpn_match = ((vaddr >> 13) == entry_vpn2);
        } else {
            vpn_match = ((vaddr >> vpn_shift) == (entry_vpn2 >> extra_bits));
        }

        if (!vpn_match) continue;

        u32 entry_asid = e.entry_hi & 0xFF;
        bool global = (e.entry_lo0 & 1) && (e.entry_lo1 & 1);
        if (!global && entry_asid != current_asid) continue;

        // Match found — determine even/odd page
        u32 even_odd_bit = (vaddr >> (vpn_shift - 1)) & 1;
        u32 entry_lo = even_odd_bit ? e.entry_lo1 : e.entry_lo0;
        bool v = (entry_lo >> 1) & 1;
        bool d = (entry_lo >> 2) & 1;

        if (!v) {
            result.hit = false;
            result.exception_code = write ? EXC_TLBS : EXC_TLBL;
            return result;
        }

        if (write && !d) {
            result.hit = false;
            result.exception_code = EXC_TLBS; // TLB Modified
            return result;
        }

        u32 pfn = (entry_lo >> 6) & 0xFFFFF;
        u32 offset = vaddr & (page_size - 1);
        result.hit = true;
        result.phys = (pfn << 12) | offset;
        return result;
    }

    // No entry found
    result.hit = false;
    result.exception_code = write ? EXC_TLBS : EXC_TLBL;
    return result;
}
