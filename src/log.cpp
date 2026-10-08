#include "log.h"

#include <cstdio>
#include <cstdarg>

static bool s_debug = false;

void log_set_debug(bool enabled) { s_debug = enabled; }

bool log_debug_enabled() { return s_debug; }

static void log_vprint(FILE* out, const char* fmt, va_list ap) {
    vfprintf(out, fmt, ap);
    fputc('\n', out);
}

void log_info(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_vprint(stdout, fmt, ap);
    va_end(ap);
}

void log_dbg(const char* fmt, ...) {
    if (!s_debug)
        return;
    va_list ap;
    va_start(ap, fmt);
    log_vprint(stdout, fmt, ap);
    va_end(ap);
}

void log_warn(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_vprint(stdout, fmt, ap);
    va_end(ap);
}

void log_err(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_vprint(stderr, fmt, ap);
    va_end(ap);
}
