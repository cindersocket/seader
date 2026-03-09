#include "sam_apdu.h"

#include <stddef.h>
#include <string.h>

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
    size_t* out_len) {
    if(!out || !out_len) return false;
    if(payload_len > 0U && !payload) return false;

    size_t total_len = 0U;
    if(extended) {
        total_len = 7U + payload_len + (explicit_le ? 2U : 0U);
        if(payload_len > 0xFFFFU) return false;
    } else {
        total_len = 5U + payload_len;
        if(payload_len > 0xFFU) return false;
    }

    if(total_len > out_cap) return false;

    out[0] = cla;
    out[1] = ins;
    out[2] = p1;
    out[3] = p2;

    if(extended) {
        out[4] = 0x00U;
        out[5] = (uint8_t)(payload_len >> 8U);
        out[6] = (uint8_t)payload_len;
        if(payload_len > 0U) {
            memcpy(out + 7U, payload, payload_len);
        }
        if(explicit_le) {
            out[7U + payload_len] = 0x00U;
            out[8U + payload_len] = 0x00U;
        }
    } else {
        out[4] = (uint8_t)payload_len;
        if(payload_len > 0U) {
            memcpy(out + 5U, payload, payload_len);
        }
    }

    *out_len = total_len;
    return true;
}

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
    size_t* out_len) {
    if(!apdu || !out_len) return false;

    size_t total_len = 0U;
    if(extended) {
        total_len = 7U + payload_len + (explicit_le ? 2U : 0U);
    } else {
        total_len = 5U + payload_len;
    }
    if(total_len > apdu_cap || payload_offset + payload_len > apdu_cap) return false;

    apdu[0] = cla;
    apdu[1] = ins;
    apdu[2] = p1;
    apdu[3] = p2;

    if(extended) {
        apdu[4] = 0x00U;
        apdu[5] = (uint8_t)(payload_len >> 8U);
        apdu[6] = (uint8_t)payload_len;
        if(payload_len > 0U && payload_offset != 7U) {
            memmove(apdu + 7U, apdu + payload_offset, payload_len);
        }
        if(explicit_le) {
            apdu[7U + payload_len] = 0x00U;
            apdu[8U + payload_len] = 0x00U;
        }
    } else {
        apdu[4] = (uint8_t)payload_len;
        if(payload_len > 0U && payload_offset != 5U) {
            memmove(apdu + 5U, apdu + payload_offset, payload_len);
        }
    }

    *out_len = total_len;
    return true;
}
