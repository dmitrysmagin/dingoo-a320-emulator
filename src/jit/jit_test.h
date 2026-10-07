#ifndef JIT_TEST_H
#define JIT_TEST_H

// Phase 1-4 discharge tests: emitter-vs-reference differential tests +
// decoder stop-classification tests. Runs compiled TBs on a real exec page
// (same VirtualAlloc/mmap path as Jit) with randomized inputs and diffs
// against jit_run_reference()/jit_run_mem_reference(). No game ROM needed.
//
// Wiring: --jit-tests runs these at startup (before SDL init) and exits
// with 0/1. Used by the Phase-1 gate.

#include "../types.h"

struct JitTestResult {
    int passed;
    int failed;
};

// Run all Phase 1-4 tests. verbose=true prints per-group progress.
JitTestResult jit_run_phase1_tests(bool verbose);

#endif // JIT_TEST_H
