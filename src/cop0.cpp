#include "cop0.h"
#include <cstdio>
#include <cstring>

void COP0::reset() {
    memset(&regs, 0, sizeof(regs));
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

void COP0::flush_ticks(u32 n, bool fast_when_wired0) {
    if (!n)
        return;
    if (fast_when_wired0 && regs.wired == 0) {
        regs.count += n;
        regs.random = (regs.random - n) & 31u;
        return;
    }
    for (u32 i = 0; i < n; i++)
        tick();
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
