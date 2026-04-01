#pragma once

#include <stdarg.h>

#include <furi.h>

#define HE_TRACE_FILE_NAME APP_DATA_PATH("trace.log")

void he_log_reset(void);
void he_log(const char* fmt, ...) _ATTRIBUTE((__format__(__printf__, 1, 2)));
void he_log_tag(const char* tag, const char* fmt, ...) _ATTRIBUTE((__format__(__printf__, 2, 3)));
