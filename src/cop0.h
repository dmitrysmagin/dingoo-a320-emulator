#ifndef COP0_H
#define COP0_H

#include "types.h"

struct COP0State {
    u32 index;      // $0
    u32 random;     // $1
    u32 entry_lo0;  // $2
    u32 entry_lo1;  // $3
    u32 context;    // $4
    u32 page_mask;  // $5
    u32 wired;      // $6
    // $7 reserved
    u32 bad_vaddr;  // $8
    u32 count;      // $9
    u32 entry_hi;   // $10
    u32 compare;    // $11
    u32 status;     // $12
    u32 cause;      // $13
    u32 epc;        // $14
    u32 prid;       // $15
    u32 config;     // $16
    // $17-$30 reserved/implementation-specific
    u32 ecc;        // $26
    u32 cache_err;  // $27
    u32 tag_lo;     // $28
    u32 err_epc;    // $30
};

struct COP0 {
    COP0State regs;

    void reset();
    u32  mfc0(int rd);
    void mtc0(int rd, u32 value);

    // TLB stubs
    void tlbp();
    void tlbr();
    void tlbwi();
    void tlbwr();
    void tick() { regs.count++; }
};

#endif // COP0_H
