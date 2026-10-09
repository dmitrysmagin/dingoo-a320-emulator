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
static constexpr u32 MAX_GOT_ENTRIES   = 256;

// Guest CPU timing (JZ4730 / Dingoo A320). Real hardware is 360 MHz; many units
// run at 420 MHz. Default matches stock 360 MHz (420 can stutter audio on some
// titles, e.g. ultimate_drift). Nominal 60 Hz frame insn budget at full CPU speed
// (reference only — not the main-loop batch size). Override batch with --quantum.
static constexpr u32 GUEST_CPU_HZ_STOCK    = 360000000u;
static constexpr u32 GUEST_CPU_HZ          = GUEST_CPU_HZ_STOCK;
static constexpr u32 GUEST_VSYNC_HZ        = 60u;
static constexpr u32 GUEST_INSNS_PER_SLICE = GUEST_CPU_HZ / GUEST_VSYNC_HZ;
// Guest insns executed per outer-loop quantum before service_os_quantum (via do_vsync).
static constexpr u32 GUEST_INSNS_PER_QUANTUM_DEFAULT = 2000000u;

// Stack
static constexpr u32 STACK_TOP         = 0x80C10000;
static constexpr u32 STACK_SIZE        = 64 * 1024;

// µC/OS-II OS_TaskReturn: a created task that falls off its entry jumps here
// instead of to $ra=0 (which walks KUSEG 0x00000000..0x4000 and halts).
static constexpr u32 TASK_RETURN_PC    = 0x80BFFC00;


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
