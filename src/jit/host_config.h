#ifndef JIT_HOST_CONFIG_H
#define JIT_HOST_CONFIG_H

// Compile-time JIT host backend (Makefile JIT_HOST=x64|arm64|x86).
#if defined(JIT_HOST_X64)
#define JIT_HOST_NAME "x64"
#elif defined(JIT_HOST_ARM64)
#define JIT_HOST_NAME "arm64"
#elif defined(JIT_HOST_X86)
#define JIT_HOST_NAME "x86"
#else
#error "Define JIT_HOST_* via Makefile (JIT_HOST=x64|arm64|x86)"
#endif

#endif
