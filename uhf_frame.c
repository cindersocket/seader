#include "uhf_frame.h"

#include <string.h>

uint8_t seader_uhf_frame_checksum(const uint8_t* data, size_t len) {
    uint32_t sum = 0;
    for(size_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return (uint8_t)(sum & 0xFFU);
}

bool seader_uhf_build_frame(
    uint8_t command,
    const uint8_t* payload,
    size_t payload_len,
    uint8_t* frame_out,
    size_t frame_out_cap,
    size_t* frame_out_len) {
    if(!frame_out || !frame_out_len || payload_len > 0xFFFFU) return false;
    if(7U + payload_len > frame_out_cap) return false;

    frame_out[0] = 0xBBU;
    frame_out[1] = 0x00U;
    frame_out[2] = command;
    frame_out[3] = (uint8_t)(payload_len >> 8U);
    frame_out[4] = (uint8_t)payload_len;
    if(payload_len > 0U && payload) memcpy(frame_out + 5U, payload, payload_len);
    frame_out[5U + payload_len] =
        seader_uhf_frame_checksum(frame_out + 1U, 4U + payload_len);
    frame_out[6U + payload_len] = 0x7EU;
    *frame_out_len = 7U + payload_len;
    return true;
}

bool seader_uhf_try_parse_frame(
    const uint8_t* stream,
    size_t stream_len,
    uint8_t expected_type,
    uint8_t expected_cmd,
    uint8_t* payload,
    size_t payload_cap,
    size_t* payload_len) {
    if(!stream || stream_len < 7U || !payload || !payload_len) return false;

    for(size_t offset = 0U; offset + 7U <= stream_len; offset++) {
        if(stream[offset] != 0xBBU) continue;

        size_t frame_payload_len = ((size_t)stream[offset + 3U] << 8U) | stream[offset + 4U];
        size_t frame_len = frame_payload_len + 7U;
        if(offset + frame_len > stream_len) break;
        if(stream[offset + frame_len - 1U] != 0x7EU) continue;
        if(seader_uhf_frame_checksum(stream + offset + 1U, frame_len - 3U) !=
           stream[offset + frame_len - 2U]) {
            continue;
        }
        if(stream[offset + 1U] != expected_type || stream[offset + 2U] != expected_cmd) continue;

        if(frame_payload_len > payload_cap) frame_payload_len = payload_cap;
        memcpy(payload, stream + offset + 5U, frame_payload_len);
        *payload_len = frame_payload_len;
        return true;
    }

    return false;
}
