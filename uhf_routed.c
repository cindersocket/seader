#include "uhf_routed.h"

#include <string.h>

#define SEADER_DER_TAG_I2C_COMMAND  0xAAU
#define SEADER_DER_TAG_OCTET_STRING 0x04U
#define SEADER_DER_TAG_INTEGER      0x02U
#define SEADER_DER_TAG_UHF_PAYLOAD  0xB6U

typedef struct {
    uint8_t tag;
    const uint8_t* value;
    size_t len;
} SeaderDerTlv;

static bool seader_der_read_length(
    const uint8_t* data,
    size_t data_len,
    size_t* offset,
    size_t* value_len) {
    if(!data || !offset || !value_len || *offset >= data_len) return false;

    uint8_t descriptor = data[(*offset)++];
    if((descriptor & 0x80U) == 0U) {
        *value_len = descriptor;
        return true;
    }

    uint8_t length_bytes = descriptor & 0x7FU;
    if(length_bytes == 0U || length_bytes > 2U || *offset + length_bytes > data_len) return false;

    size_t parsed = 0U;
    for(uint8_t i = 0U; i < length_bytes; i++) {
        parsed = (parsed << 8U) | data[(*offset)++];
    }

    *value_len = parsed;
    return true;
}

static bool
    seader_der_read_tlv(const uint8_t* data, size_t data_len, size_t* offset, SeaderDerTlv* tlv) {
    if(!data || !offset || !tlv || *offset >= data_len) return false;

    tlv->tag = data[(*offset)++];
    if(!seader_der_read_length(data, data_len, offset, &tlv->len)) return false;
    if(*offset + tlv->len > data_len) return false;

    tlv->value = data + *offset;
    *offset += tlv->len;
    return true;
}

static bool seader_der_parse_positive_integer(const SeaderDerTlv* tlv, uint32_t* value) {
    if(!tlv || !value || tlv->tag != SEADER_DER_TAG_INTEGER || tlv->len == 0U || tlv->len > 5U) {
        return false;
    }

    const uint8_t* bytes = tlv->value;
    size_t len = tlv->len;

    if(len > 1U && bytes[0] == 0x00U) {
        bytes++;
        len--;
    }
    if(len > 4U) return false;

    uint32_t parsed = 0U;
    for(size_t i = 0U; i < len; i++) {
        parsed = (parsed << 8U) | bytes[i];
    }
    *value = parsed;
    return true;
}

static SeaderUhfRoutedCommand seader_uhf_routed_map_command_tag(uint8_t command_tag) {
    uint8_t command_index = (uint8_t)(command_tag & 0x1FU);

    switch(command_index) {
    case 0U:
        return SeaderUhfRoutedCommandGetVersion;
    case 4U:
        return SeaderUhfRoutedCommandGetProperties;
    case 6U:
        return SeaderUhfRoutedCommandSetAccessPassword;
    case 7U:
        return SeaderUhfRoutedCommandGetPrivateData;
    default:
        return SeaderUhfRoutedCommandNone;
    }
}

bool seader_uhf_routed_try_parse_apdu(
    const uint8_t* apdu,
    size_t apdu_len,
    SeaderUhfRoutedFrame* frame_out) {
    if(!apdu || !frame_out || apdu_len < 8U) return false;

    size_t payload_offset = 6U;
    size_t payload_len = apdu_len - payload_offset;
    const uint8_t* payload = apdu + payload_offset;

    size_t offset = 0U;
    SeaderDerTlv i2c_tlv = {0};
    SeaderDerTlv i2c_header_tlv = {0};
    SeaderDerTlv bus_addr_tlv = {0};
    SeaderDerTlv nested_payload_tlv = {0};

    if(!seader_der_read_tlv(payload, payload_len, &offset, &i2c_tlv)) return false;
    if(i2c_tlv.tag != SEADER_DER_TAG_I2C_COMMAND) return false;

    size_t i2c_offset = 0U;
    if(!seader_der_read_tlv(i2c_tlv.value, i2c_tlv.len, &i2c_offset, &i2c_header_tlv))
        return false;
    if(i2c_header_tlv.tag != SEADER_DER_TAG_OCTET_STRING || i2c_header_tlv.len != 6U) return false;

    if(!seader_der_read_tlv(i2c_tlv.value, i2c_tlv.len, &i2c_offset, &bus_addr_tlv)) return false;

    uint32_t bus_addr = 0U;
    if(!seader_der_parse_positive_integer(&bus_addr_tlv, &bus_addr)) return false;

    if(!seader_der_read_tlv(i2c_tlv.value, i2c_tlv.len, &i2c_offset, &nested_payload_tlv))
        return false;
    if(nested_payload_tlv.tag != SEADER_DER_TAG_UHF_PAYLOAD) return false;

    uint8_t command_tag = 0U;
    const uint8_t* command_value = NULL;
    size_t command_len = 0U;

    size_t uhf_offset = 0U;
    SeaderDerTlv command_tlv = {0};
    bool command_tlv_ok = seader_der_read_tlv(
        nested_payload_tlv.value, nested_payload_tlv.len, &uhf_offset, &command_tlv);

    if(command_tlv_ok) {
        command_tag = command_tlv.tag;
        command_value = command_tlv.value;
        command_len = command_tlv.len;
    } else {
        size_t trailing_len = 0U;
        const uint8_t* trailing = NULL;
        if(i2c_offset <= i2c_tlv.len) {
            trailing_len = i2c_tlv.len - i2c_offset;
            trailing = i2c_tlv.value + i2c_offset;
        }

        if(nested_payload_tlv.len == 2U && nested_payload_tlv.value && trailing &&
           trailing_len > 0U) {
            uint8_t recovered_tag = nested_payload_tlv.value[0];
            uint8_t recovered_len = nested_payload_tlv.value[1];
            if(((recovered_tag & 0xC0U) == 0x80U) && recovered_len == trailing_len) {
                command_tag = recovered_tag;
                command_value = trailing;
                command_len = trailing_len;
                command_tlv_ok = true;
            }
        }
    }

    if(!command_tlv_ok) return false;

    memset(frame_out, 0, sizeof(*frame_out));
    for(size_t i = 0U; i < 6U; i++) {
        frame_out->route_header[i] = apdu[i];
        frame_out->i2c_header[i] = i2c_header_tlv.value[i];
    }
    frame_out->bus_address = bus_addr;
    frame_out->command = seader_uhf_routed_map_command_tag(command_tag);
    if(frame_out->command == SeaderUhfRoutedCommandNone) return false;

    if(command_len > sizeof(frame_out->command_value)) return false;
    if(command_len > 0U && command_value) {
        memcpy(frame_out->command_value, command_value, command_len);
    }
    frame_out->command_value_len = command_len;
    return true;
}
