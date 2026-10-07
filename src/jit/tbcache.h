#ifndef JIT_TBCACHE_H
#define JIT_TBCACHE_H

#include "../types.h"

#include <vector>

struct JitState;  // jit.h (only a pointer target here, so this is enough)
typedef u32 (*JitTbFunc)(JitState* state);  // identical to Jit's TbFunc

// Phase 6b direct-mapped TB cache: guest entry vaddr -> compiled TB.
//
// One slot per hash bucket; collisions evict (a ~2us recompile, rare).
// Null func = negative entry (known-uncompilable pc — same lookup cost
// as a hit, which killed the repeat-formation overhead on JR-heads).
// Whole-table clear on gen/phase flush; no per-entry lifetime tracking.
// Lookup is a hash + tag compare (~9ns measured) instead of an
// unordered_map find.
class JitTbCache {
public:
    static constexpr u32 kBits = 14;
    static constexpr u32 kSize = 1u << kBits;  // 16384 slots x 16 B = 256 KB
    static constexpr u32 kMask = kSize - 1;
    // Never a valid entry pc: KSEG3, always fails eligibility (unmapped).
    static constexpr u32 kEmpty = 0xFFFFFFFFu;

    struct Slot {
        u32 key;
        JitTbFunc func;
        u32 count;  // guest execute_one-equivalents (ticks + insn_count)
    };

    JitTbCache()
        : m_table(kSize, Slot{kEmpty, nullptr, 0})
        , m_used(0) {
    }

    // Tag match (func may be null = negative entry). No stats inside;
    // the caller counts hits/negatives (branch-free here keeps it fast).
    bool find(u32 pc, JitTbFunc& func, u32& count) const {
        const Slot& s = m_table[(pc >> 2) & kMask];
        if (s.key != pc)
            return false;
        func = s.func;
        count = s.count;
        return true;
    }

    // Insert or refresh. Returns true when a *different* live entry was
    // evicted (caller counts it; refreshes and empty fills don't count).
    bool insert(u32 pc, JitTbFunc func, u32 count) {
        Slot& s = m_table[(pc >> 2) & kMask];
        bool evicted = (s.key != kEmpty && s.key != pc);
        if (s.key == kEmpty)
            m_used++;
        s.key = pc;
        s.func = func;
        s.count = count;
        return evicted;
    }

    void clear() {
        for (auto& s : m_table) {
            s.key = kEmpty;
            s.func = nullptr;
            s.count = 0;
        }
        m_used = 0;
    }

    u32 used() const { return m_used; }
    u32 capacity() const { return kSize; }

private:
    std::vector<Slot> m_table;
    u32 m_used;
};

#endif  // JIT_TBCACHE_H
