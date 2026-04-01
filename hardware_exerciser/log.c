#include "log.h"

#include <storage/storage.h>
#include <toolbox/stream/buffered_file_stream.h>

#define HE_LOG_TAG "HETrace"

static void he_log_write_line(const char* line, FS_OpenMode open_mode) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, STORAGE_APP_DATA_PATH_PREFIX);

    Stream* stream = buffered_file_stream_alloc(storage);
    if(buffered_file_stream_open(stream, HE_TRACE_FILE_NAME, FSAM_READ_WRITE, open_mode)) {
        stream_seek(stream, 0, StreamOffsetFromEnd);
        stream_write(stream, (const uint8_t*)line, strlen(line));
    }

    buffered_file_stream_close(stream);
    stream_free(stream);
    furi_record_close(RECORD_STORAGE);
}

void he_log_reset(void) {
    he_log_write_line("", FSOM_CREATE_ALWAYS);
}

static void he_log_vwrite(const char* tag, const char* fmt, va_list args) {
    char message[192];
    vsnprintf(message, sizeof(message), fmt, args);

    char line[256];
    snprintf(line, sizeof(line), "[%s] %s\n", tag ? tag : HE_LOG_TAG, message);
    he_log_write_line(line, FSOM_OPEN_ALWAYS);
}

void he_log(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    he_log_vwrite(HE_LOG_TAG, fmt, args);
    va_end(args);
}

void he_log_tag(const char* tag, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    he_log_vwrite(tag, fmt, args);
    va_end(args);
}
