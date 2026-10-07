#ifndef JIT_TBCACHE_H
#define JIT_TBCACHE_H

#include "../types.h"

#include <vector>

// Recorded during branch-TB emit; stored in the cache slot for later patching.
struct JitChainInfo {
    static constexpr u32 kMaxSites = 2;
    u32 n = 0;
    struct Site {
        u32 code_off;
        u32 target_pc;
    } sites[kMaxSites];
};

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

    // Phase 6c chain metadata: exit-site table owned by the cached TB.
    // Kept as plain offsets + PCs (no emit.h dependency — tbcache.h must
    // stay includable from jit.h, which emit.h itself includes).
    struct Slot {
        u32 key;
        JitTbFunc func;
        u32 count;  // guest execute_one-equivalents (ticks + insn_count)
        u8 chain_n;
        u16 chain_off[2];
        u32 chain_target[2];
        u8 tick_fast;  // 1: TB has no MTC0 — COP0 flush may batch if wired==0
    };

    JitTbCache()
        : m_table(kSize, Slot{})
        , m_used(0) {
        for (auto& s : m_table) {
            s.key = kEmpty;
            s.func = nullptr;
            s.count = 0;
            s.chain_n = 0;
            s.tick_fast = 0;
        }
    }

    // Tag match (func may be null = negative entry). No stats inside;
    // the caller counts hits/negatives (branch-free here keeps it fast).
    bool find(u32 pc, JitTbFunc& func, u32& count, bool* tick_fast = nullptr) const {
        const Slot& s = m_table[(pc >> 2) & kMask];
        if (s.key != pc)
            return false;
        func = s.func;
        count = s.count;
        if (tick_fast)
            *tick_fast = s.tick_fast != 0;
        return true;
    }

    // Full slot access for chaining (patch source sites after a NEXT_PC).
    // Returns null when pc is not cached.
    const Slot* find_slot(u32 pc) const {
        const Slot& s = m_table[(pc >> 2) & kMask];
        return (s.key == pc) ? &s : nullptr;
    }

    // Insert or refresh. Returns true when a *different* live entry was
    // evicted (caller counts it; refreshes and empty fills don't count).
    // Chain metadata is stored alongside; clear() drops it with the TB.
    bool insert(u32 pc, JitTbFunc func, u32 count, bool tick_fast,
                const JitChainInfo* chain) {
        Slot& s = m_table[(pc >> 2) & kMask];
        bool evicted = (s.key != kEmpty && s.key != pc);
        if (s.key == kEmpty)
            m_used++;
        s.key = pc;
        s.func = func;
        s.count = count;
        s.tick_fast = tick_fast ? 1 : 0;
        s.chain_n = 0;
        if (chain) {
            for (u32 i = 0; i < chain->n && i < 2; i++) {
                s.chain_off[i] = (u16)chain->sites[i].code_off;
                s.chain_target[i] = chain->sites[i].target_pc;
            }
            s.chain_n = chain->n;
        }

        return evicted;
    }

    // Patch branch exits that target `target_pc` once that TB is cached.
    template <typename PatchFn>
    u32 patch_edges_to(u32 target_pc, u8* chain_entry, PatchFn patch) const {
        u32 n = 0;
        for (const Slot& s : m_table) {
            if (!s.func || s.key == kEmpty)
                continue;
            for (u32 i = 0; i < s.chain_n; i++) {
                if (s.chain_target[i] != target_pc)
                    continue;
                patch((u8*)s.func, s.chain_off[i], chain_entry);
                n++;
            }
        }
        return n;
    }

    void clear() {
        for (auto& s : m_table) {
            s.key = kEmpty;
            s.func = nullptr;
            s.count = 0;
            s.chain_n = 0;
            s.tick_fast = 0;
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
