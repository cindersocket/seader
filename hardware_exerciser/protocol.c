#include "protocol.h"

#include <string.h>

/* CCID uses a fixed 10-byte header; the wire format adds 2 bytes of framing in front of it. */
#define HX_CCID_HEADER_LEN 10U
#define HX_WIRE_HEADER_LEN (2U + HX_CCID_HEADER_LEN)

/* CCID length fields are encoded little-endian in bytes 3..6 of the on-wire frame. */
static uint32_t hx_read_le32(const uint8_t* data) {
    return ((uint32_t)data[0]) | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

uint8_t hx_calc_lrc(const uint8_t* data, size_t len) {
    uint8_t lrc = 0U;

    /* The transport uses a simple XOR LRC so the same routine works for requests and responses. */
    for(size_t i = 0; i < len; i++) {
        lrc ^= data[i];
    }

    return lrc;
}

bool hx_validate_lrc(const uint8_t* data, size_t len) {
    if(len == 0U) {
        return false;
    }

    return hx_calc_lrc(data, len - 1U) == data[len - 1U];
}

size_t hx_add_lrc(uint8_t* data, size_t len) {
    data[len] = hx_calc_lrc(data, len);
    return len + 1U;
}

HxFrameState
    hx_ccid_frame_state(const uint8_t* data, size_t len, size_t max_frame_len, size_t* frame_len) {
    if(frame_len) {
        *frame_len = 0U;
    }

    if(len < 2U) {
        return HxFrameStateNeedMore;
    }

    if(data[0] != HX_SYNC || data[1] != HX_CTRL) {
        /* The caller can discard a byte and keep scanning for the framing preamble. */
        return HxFrameStateDesync;
    }

    if(len < HX_WIRE_HEADER_LEN) {
        return HxFrameStateNeedMore;
    }

    /* The CCID payload length does not include the wire preamble or final LRC byte. */
    const uint32_t payload_len = hx_read_le32(&data[3]);
    if((size_t)payload_len > SIZE_MAX - (HX_WIRE_HEADER_LEN + 1U)) {
        /* Guard against size_t overflow before building the full frame length. */
        return HxFrameStateInvalidLength;
    }

    const size_t total_len = HX_WIRE_HEADER_LEN + (size_t)payload_len + 1U;
    if(total_len > max_frame_len) {
        if(frame_len) {
            *frame_len = total_len;
        }
        return HxFrameStateInvalidLength;
    }

    if(frame_len) {
        *frame_len = total_len;
    }

    if(len < total_len) {
        return HxFrameStateNeedMore;
    }

    return HxFrameStateReady;
}

bool hx_ccid_is_escape_frame(const uint8_t* frame, size_t frame_len) {
    size_t parsed_len = 0U;
    if(hx_ccid_frame_state(frame, frame_len, frame_len, &parsed_len) != HxFrameStateReady) {
        return false;
    }

    /* Only escape frames are consumed locally; other CCID traffic is forwarded to the SAM UART. */
    return frame[2] == HX_CCID_PC_TO_RDR_ESCAPE;
}

bool hx_parse_request_payload(const uint8_t* payload, size_t payload_len, HxRequest* request) {
    if(!payload || !request || payload_len < 4U) {
        return false;
    }

    if(payload[0] != HX_REQUEST_MAGIC_0 || payload[1] != HX_REQUEST_MAGIC_1 ||
       payload[2] != HX_PROTOCOL_VERSION) {
        return false;
    }

    request->opcode = payload[3];
    /* The body points into the caller-owned payload buffer and is never copied here. */
    request->body = payload + 4U;
    request->body_len = payload_len - 4U;
    return true;
}

bool hx_copy_response_body(
    const uint8_t* body,
    size_t body_len,
    uint8_t* out,
    size_t out_cap,
    size_t* out_len) {
    if(out_len) {
        *out_len = 0U;
    }

    if(body_len > 0U && (!body || !out || body_len > out_cap)) {
        return false;
    }

    if(body_len > 0U) {
        memcpy(out, body, body_len);
    }

    if(out_len) {
        *out_len = body_len;
    }

    return true;
}

bool hx_parse_gpio_configure_request(
    const uint8_t* body,
    size_t body_len,
    HxGpioConfigureRequest* request) {
    if(!body || !request || body_len != 4U) {
        return false;
    }

    request->pin_number = body[0];
    request->mode = body[1];
    request->pull = body[2];
    request->initial_level = body[3];
    return true;
}

bool hx_parse_gpio_write_request(
    const uint8_t* body,
    size_t body_len,
    HxGpioWriteRequest* request) {
    if(!body || !request || body_len != 2U) {
        return false;
    }

    request->pin_number = body[0];
    request->level = body[1];
    return true;
}

bool hx_parse_gpio_pin_request(const uint8_t* body, size_t body_len, uint8_t* pin_number) {
    if(!body || !pin_number || body_len != 1U) {
        return false;
    }

    *pin_number = body[0];
    return true;
}

bool hx_parse_gpio_vector_request(
    const uint8_t* body,
    size_t body_len,
    HxGpioVectorRequest* request) {
    if(!body || !request || body_len < 6U) {
        return false;
    }

    const size_t write_count = body[0];
    const size_t writes_len = write_count * 2U;
    if(writes_len > body_len - 6U) {
        return false;
    }

    const size_t settle_offset = 1U + writes_len;
    const size_t sample_count_offset = settle_offset + 4U;
    if(sample_count_offset >= body_len) {
        return false;
    }

    const size_t sample_count = body[sample_count_offset];
    const size_t samples_len = sample_count * 2U;
    if(samples_len != body_len - (sample_count_offset + 1U)) {
        return false;
    }

    request->writes = body + 1U;
    request->write_count = write_count;
    request->settle_us = hx_read_le32(body + settle_offset);
    request->samples = body + sample_count_offset + 1U;
    request->sample_count = sample_count;
    return true;
}

bool hx_gpio_vector_write_at(
    const HxGpioVectorRequest* request,
    size_t index,
    HxGpioWriteRequest* write) {
    if(!request || !write || index >= request->write_count) {
        return false;
    }

    write->pin_number = request->writes[index * 2U];
    write->level = request->writes[index * 2U + 1U];
    return true;
}

bool hx_gpio_vector_sample_at(
    const HxGpioVectorRequest* request,
    size_t index,
    HxGpioVectorSample* sample) {
    if(!request || !sample || index >= request->sample_count) {
        return false;
    }

    sample->pin_number = request->samples[index * 2U];
    sample->sample_kind = request->samples[index * 2U + 1U];
    return true;
}

bool hx_parse_uhf_power_request(
    const uint8_t* body,
    size_t body_len,
    HxUhfPowerRequest* request) {
    if(!body || !request || body_len != 1U) {
        return false;
    }

    if(body[0] > HxUhfPowerActionHibernate) {
        return false;
    }

    request->action = body[0];
    return true;
}

bool hx_parse_uhf_bridge_control_request(
    const uint8_t* body,
    size_t body_len,
    HxUhfBridgeControlRequest* request) {
    if(!body || !request || body_len != 1U) {
        return false;
    }

    if(body[0] > HxUhfBridgeActionForceEnable) {
        return false;
    }

    request->action = body[0];
    return true;
}

uint8_t hx_board_classify(bool pa4_high, bool pc1_high, bool pc0_high) {
    if(pa4_high) {
        return HxBoardClassUhfCarrier;
    }

    if(pc1_high || pc0_high) {
        return HxBoardClassSamOnly;
    }

    return HxBoardClassNone;
}

uint8_t hx_uhf_frame_checksum(const uint8_t* data, size_t len) {
    uint32_t sum = 0U;
    if(!data) {
        return 0U;
    }

    for(size_t i = 0U; i < len; i++) {
        sum += data[i];
    }
    return (uint8_t)(sum & 0xffU);
}

bool hx_uhf_build_frame(
    uint8_t command,
    const uint8_t* payload,
    size_t payload_len,
    uint8_t* frame_out,
    size_t frame_out_cap,
    size_t* frame_out_len) {
    if(!frame_out || !frame_out_len || payload_len > 0xffffU) {
        return false;
    }
    if(payload_len > 0U && !payload) {
        return false;
    }
    if(frame_out_cap < 7U + payload_len) {
        return false;
    }

    frame_out[0] = 0xbbU;
    frame_out[1] = 0x00U;
    frame_out[2] = command;
    frame_out[3] = (uint8_t)(payload_len >> 8U);
    frame_out[4] = (uint8_t)payload_len;
    if(payload_len > 0U) {
        memcpy(frame_out + 5U, payload, payload_len);
    }
    frame_out[5U + payload_len] =
        hx_uhf_frame_checksum(frame_out + 1U, 4U + payload_len);
    frame_out[6U + payload_len] = 0x7eU;
    *frame_out_len = 7U + payload_len;
    return true;
}

bool hx_uhf_try_parse_frame(
    const uint8_t* stream,
    size_t stream_len,
    uint8_t expected_type,
    uint8_t expected_cmd,
    uint8_t* payload,
    size_t payload_cap,
    size_t* payload_len) {
    if(!stream || stream_len < 7U || !payload || !payload_len) {
        return false;
    }

    for(size_t offset = 0U; offset + 7U <= stream_len; offset++) {
        if(stream[offset] != 0xbbU) {
            continue;
        }

        const size_t frame_payload_len =
            ((size_t)stream[offset + 3U] << 8U) | stream[offset + 4U];
        const size_t frame_len = frame_payload_len + 7U;
        if(offset + frame_len > stream_len) {
            continue;
        }
        if(stream[offset + frame_len - 1U] != 0x7eU) {
            continue;
        }
        if(hx_uhf_frame_checksum(stream + offset + 1U, frame_len - 3U) !=
           stream[offset + frame_len - 2U]) {
            continue;
        }
        if(stream[offset + 1U] != expected_type || stream[offset + 2U] != expected_cmd) {
            continue;
        }

        if(frame_payload_len > payload_cap) {
            continue;
        }
        memcpy(payload, stream + offset + 5U, frame_payload_len);
        *payload_len = frame_payload_len;
        return true;
    }

    return false;
}

HxUhfProfile hx_uhf_profile_from_versions(const char* hw_version, const char* sw_version) {
    HxUhfProfile profile = {
        .family = HxUhfModuleFamilyUnknown,
        .hardware_class = HxUhfHardwareClassUnknown,
    };
    (void)sw_version;

    if(!hw_version) {
        return profile;
    }

    if(strstr(hw_version, "QM100")) {
        profile.family = HxUhfModuleFamilyQM100;
    } else if(strstr(hw_version, "M100")) {
        profile.family = HxUhfModuleFamilyM100;
    }

    if(strstr(hw_version, "30dBm")) {
        profile.hardware_class = HxUhfHardwareClass30dBm;
    } else if(strstr(hw_version, "26dBm")) {
        profile.hardware_class = HxUhfHardwareClass26dBm;
    } else if(strstr(hw_version, "20dBm")) {
        profile.hardware_class = HxUhfHardwareClass20dBm;
    }

    return profile;
}

size_t hx_build_hf14a_scan_payload(
    const uint8_t* uid,
    size_t uid_len,
    const uint8_t atqa[2],
    uint8_t sak,
    const uint8_t* ats,
    size_t ats_len,
    uint8_t* out,
    size_t out_cap) {
    const size_t total_len = 5U + uid_len + ats_len;

    if(!uid || !atqa || !out || uid_len > 255U || ats_len > 255U || out_cap < total_len) {
        return 0U;
    }

    /* The host expects a compact self-describing blob rather than a packed C struct. */
    out[0] = (uint8_t)uid_len;
    memcpy(out + 1U, uid, uid_len);
    out[1U + uid_len] = atqa[0];
    out[2U + uid_len] = atqa[1];
    out[3U + uid_len] = sak;
    out[4U + uid_len] = (uint8_t)ats_len;
    if(ats_len > 0U) {
        if(!ats) {
            return 0U;
        }
        memcpy(out + 5U + uid_len, ats, ats_len);
    }

    return total_len;
}

static size_t hx_copy_string_field(const char* value, uint8_t* out, size_t out_cap) {
    if(!value || !out || out_cap == 0U) {
        return 0U;
    }

    const size_t len = strlen(value);
    if(len > 255U || out_cap < 1U + len) {
        return 0U;
    }

    out[0] = (uint8_t)len;
    if(len > 0U) {
        memcpy(out + 1U, value, len);
    }
    return 1U + len;
}

size_t hx_build_response_payload(
    uint8_t opcode,
    uint8_t status,
    const uint8_t* body,
    size_t body_len,
    uint8_t* out,
    size_t out_cap) {
    const size_t total_len = 5U + body_len;

    if(!out || out_cap < total_len) {
        return 0U;
    }

    /* Response payloads intentionally mirror the request header with an added status byte. */
    out[0] = HX_REQUEST_MAGIC_0;
    out[1] = HX_REQUEST_MAGIC_1;
    out[2] = HX_PROTOCOL_VERSION;
    out[3] = opcode;
    out[4] = status;

    if(body_len > 0U && body) {
        for(size_t i = 0U; i < body_len; i++) {
            out[5U + i] = body[i];
        }
    }

    return total_len;
}

size_t hx_build_board_snapshot_payload(
    uint8_t board_class,
    uint8_t pa4_high,
    uint8_t pc1_high,
    uint8_t pc0_high,
    uint8_t pc3_level,
    uint8_t otg_enabled,
    uint8_t otg_fault,
    uint16_t vbus_mv,
    uint8_t* out,
    size_t out_cap) {
    if(!out || out_cap < 10U) {
        return 0U;
    }

    out[0] = board_class;
    out[1] = pa4_high ? 1U : 0U;
    out[2] = pc1_high ? 1U : 0U;
    out[3] = pc0_high ? 1U : 0U;
    out[4] = pc3_level ? 1U : 0U;
    out[5] = otg_enabled ? 1U : 0U;
    out[6] = otg_fault ? 1U : 0U;
    out[7] = 0U;
    out[8] = (uint8_t)(vbus_mv & 0xffU);
    out[9] = (uint8_t)(vbus_mv >> 8U);
    return 10U;
}

size_t hx_build_uhf_probe_payload(
    uint8_t presence,
    uint8_t family,
    uint8_t hardware_class,
    const char* hw_version,
    const char* sw_version,
    uint8_t* out,
    size_t out_cap) {
    if(!out || out_cap < 3U) {
        return 0U;
    }

    out[0] = presence;
    out[1] = family;
    out[2] = hardware_class;
    size_t used = 3U;

    const size_t hw_len = hx_copy_string_field(hw_version ? hw_version : "", out + used, out_cap - used);
    if(hw_len == 0U) {
        return 0U;
    }
    used += hw_len;

    const size_t sw_len = hx_copy_string_field(sw_version ? sw_version : "", out + used, out_cap - used);
    if(sw_len == 0U) {
        return 0U;
    }
    used += sw_len;
    return used;
}

size_t hx_build_uhf_power_payload(
    uint8_t power_enabled,
    uint8_t otg_enabled,
    uint8_t otg_fault,
    uint16_t vbus_mv,
    uint8_t* out,
    size_t out_cap) {
    if(!out || out_cap < 6U) {
        return 0U;
    }

    out[0] = power_enabled ? 1U : 0U;
    out[1] = otg_enabled ? 1U : 0U;
    out[2] = otg_fault ? 1U : 0U;
    out[3] = 0U;
    out[4] = (uint8_t)(vbus_mv & 0xffU);
    out[5] = (uint8_t)(vbus_mv >> 8U);
    return 6U;
}

size_t hx_build_uhf_bridge_payload(
    uint8_t bridge_enabled,
    uint8_t forced,
    uint8_t module_present,
    uint8_t* out,
    size_t out_cap) {
    if(!out || out_cap < 3U) {
        return 0U;
    }

    out[0] = bridge_enabled ? 1U : 0U;
    out[1] = forced ? 1U : 0U;
    out[2] = module_present ? 1U : 0U;
    return 3U;
}

size_t hx_build_gpio_pin_info(
    uint8_t pin_number,
    uint8_t flags,
    const char* name,
    uint8_t* out,
    size_t out_cap) {
    if(!name || !out) {
        return 0U;
    }

    const size_t name_len = strlen(name);
    const size_t total_len = 3U + name_len;
    if(name_len > 255U || out_cap < total_len) {
        return 0U;
    }

    out[0] = pin_number;
    out[1] = flags;
    out[2] = (uint8_t)name_len;
    memcpy(out + 3U, name, name_len);
    return total_len;
}

size_t hx_build_gpio_analog_value(
    uint16_t raw_value,
    uint16_t millivolts,
    uint8_t* out,
    size_t out_cap) {
    if(!out || out_cap < 4U) {
        return 0U;
    }

    out[0] = (uint8_t)(raw_value & 0xffU);
    out[1] = (uint8_t)((raw_value >> 8) & 0xffU);
    out[2] = (uint8_t)(millivolts & 0xffU);
    out[3] = (uint8_t)((millivolts >> 8) & 0xffU);
    return 4U;
}

size_t hx_build_gpio_vector_sample(
    uint8_t pin_number,
    uint8_t sample_kind,
    uint16_t value,
    uint16_t aux,
    uint8_t* out,
    size_t out_cap) {
    if(!out || out_cap < 6U) {
        return 0U;
    }

    out[0] = pin_number;
    out[1] = sample_kind;
    out[2] = (uint8_t)(value & 0xffU);
    out[3] = (uint8_t)((value >> 8) & 0xffU);
    out[4] = (uint8_t)(aux & 0xffU);
    out[5] = (uint8_t)((aux >> 8) & 0xffU);
    return 6U;
}

size_t hx_build_ccid_escape_response(
    const uint8_t* request_frame,
    size_t request_frame_len,
    const uint8_t* payload,
    size_t payload_len,
    uint8_t* out,
    size_t out_cap) {
    size_t frame_len = 0U;

    if(!request_frame || !out ||
       hx_ccid_frame_state(request_frame, request_frame_len, request_frame_len, &frame_len) !=
           HxFrameStateReady ||
       frame_len != request_frame_len) {
        return 0U;
    }

    const size_t total_len = HX_WIRE_HEADER_LEN + payload_len + 1U;
    if(out_cap < total_len) {
        return 0U;
    }

    for(size_t i = 0U; i < total_len; i++) {
        out[i] = 0U;
    }

    out[0] = HX_SYNC;
    out[1] = HX_CTRL;
    out[2] = HX_CCID_RDR_TO_PC_ESCAPE;
    out[3] = (uint8_t)(payload_len & 0xffU);
    out[4] = (uint8_t)((payload_len >> 8) & 0xffU);
    out[5] = (uint8_t)((payload_len >> 16) & 0xffU);
    out[6] = (uint8_t)((payload_len >> 24) & 0xffU);
    /* Preserve host-visible correlation fields from the request. */
    out[7] = request_frame[7];
    out[8] = request_frame[8];
    out[9] = 0U;
    out[10] = 0U;
    out[11] = 0U;

    for(size_t i = 0U; i < payload_len; i++) {
        out[HX_WIRE_HEADER_LEN + i] = payload[i];
    }

    return hx_add_lrc(out, HX_WIRE_HEADER_LEN + payload_len);
}
