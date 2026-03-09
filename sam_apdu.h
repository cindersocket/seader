#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool seader_build_command_apdu(
    bool extended,
    bool explicit_le,
    uint8_t cla,
    uint8_t ins,
    uint8_t p1,
    uint8_t p2,
    const uint8_t* payload,
    size_t payload_len,
    uint8_t* out,
    size_t out_cap,
    size_t* out_len);

bool seader_build_command_apdu_inplace(
    bool extended,
    bool explicit_le,
    uint8_t cla,
    uint8_t ins,
    uint8_t p1,
    uint8_t p2,
    uint8_t* apdu,
    size_t apdu_cap,
    size_t payload_offset,
    size_t payload_len,
    size_t* out_len);
