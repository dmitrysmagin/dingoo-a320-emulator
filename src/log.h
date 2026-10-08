#ifndef EMU_LOG_H
#define EMU_LOG_H

// Console logging: default shows startup + summary; --debug enables log_dbg().
void log_set_debug(bool enabled);
bool log_debug_enabled();

void log_info(const char* fmt, ...);
void log_dbg(const char* fmt, ...);
void log_warn(const char* fmt, ...);
void log_err(const char* fmt, ...);

#endif
