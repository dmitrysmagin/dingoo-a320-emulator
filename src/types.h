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
static constexpr u32 RAM_SIZE          = 16 * 1024 * 1024;  // 16 MB
static constexpr u32 KSEG0_BASE        = 0x80000000;
static constexpr u32 KSEG1_BASE        = 0xA0000000;
static constexpr u32 KSEG_MASK         = 0xE0000000;
static constexpr u32 KSEG0_KSEG1_MASK  = 0x1FFFFFFF;
static constexpr u32 PHYS_MASK         = 0x00FFFFFF;

// Load addresses from binary analysis
static constexpr u32 RAWD_LOAD_ADDR    = 0x80A00000;
static constexpr u32 RAWD_LOAD_PHYS    = 0x00A00000;
static constexpr u32 BSS_START_ADDR    = 0x80B41CE0;
static constexpr u32 BSS_END_ADDR      = 0x80B44270;
static constexpr u32 DL_MAIN_ADDR      = 0x80AD6A20;
static constexpr u32 APP_MAIN_ADDR     = 0x80AD6B1C;
static constexpr u32 GOT_BASE          = 0x80AD67E0;
static constexpr u32 GOT_ENTRY_SIZE    = 8;
static constexpr u32 GOT_COUNT         = 72;

// Stack
static constexpr u32 STACK_TOP         = 0x80C10000;
static constexpr u32 STACK_SIZE        = 64 * 1024;

// Resource section offset in 7days.app
static constexpr u64 RESOURCE_OFFSET   = 0x150000;

// Exception codes
static constexpr u32 EXC_INT    = 0;
static constexpr u32 EXC_MOD   = 1;
static constexpr u32 EXC_TLBL  = 2;
static constexpr u32 EXC_TLBS  = 3;
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
