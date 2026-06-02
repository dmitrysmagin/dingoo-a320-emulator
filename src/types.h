#ifndef TYPES_H
#define TYPES_H

#include <cstdint>
#include <cstring>

using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using s8  = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;

// Memory constants
static constexpr u32 RAM_SIZE          = 32 * 1024 * 1024;   // 32 MB — matches real Dingoo A320 hardware
static constexpr u32 KSEG0_BASE        = 0x80000000;
static constexpr u32 KSEG1_BASE        = 0xA0000000;
static constexpr u32 KSEG_MASK         = 0xE0000000;
static constexpr u32 KSEG0_KSEG1_MASK  = 0x1FFFFFFF;
static constexpr u32 PHYS_MASK         = 0x00FFFFFF;

// Load addresses from binary analysis
static constexpr u32 RAWD_LOAD_ADDR    = 0x80A00000;
static constexpr u32 RAWD_LOAD_PHYS    = 0x00A00000;
static constexpr u32 GOT_ENTRY_SIZE    = 8;
static constexpr u32 MAX_GOT_ENTRIES   = 96;

// Stack
static constexpr u32 STACK_TOP         = 0x80C10000;
static constexpr u32 STACK_SIZE        = 64 * 1024;


// Exception codes
static constexpr u32 EXC_INT    = 0;
static constexpr u32 EXC_MOD   = 1;
// EXC_TLBL/TLBS removed: TLB emulation deleted; unhandled TLBR/TLBWI/TLBWR/TLBP traps to EXC_RI
static constexpr u32 EXC_ADEL  = 4;
static constexpr u32 EXC_ADES  = 5;
static constexpr u32 EXC_IBE   = 6;
static constexpr u32 EXC_DBE   = 7;
static constexpr u32 EXC_SYS   = 8;
static constexpr u32 EXC_BP    = 9;
static constexpr u32 EXC_RI    = 10;
static constexpr u32 EXC_CPU   = 11;
static constexpr u32 EXC_OV    = 12;
static constexpr u32 EXC_TR    = 13;

#endif // TYPES_H
