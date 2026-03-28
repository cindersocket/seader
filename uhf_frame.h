#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

uint8_t seader_uhf_frame_checksum(const uint8_t* data, size_t len);
bool seader_uhf_build_frame(
    uint8_t command,
    const uint8_t* payload,
    size_t payload_len,
    uint8_t* frame_out,
    size_t frame_out_cap,
    size_t* frame_out_len);
bool seader_uhf_try_parse_frame(
    const uint8_t* stream,
    size_t stream_len,
    uint8_t expected_type,
    uint8_t expected_cmd,
    uint8_t* payload,
    size_t payload_cap,
    size_t* payload_len);
