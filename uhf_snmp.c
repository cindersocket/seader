#include "uhf_snmp.h"

#include <string.h>

typedef struct {
    uint8_t tag;
    const uint8_t* value;
    size_t len;
} SeaderUhfSnmpTlv;

static bool
    seader_uhf_snmp_append_len(uint8_t* dst, size_t dst_size, size_t* offset, size_t value_len) {
    if(value_len < 0x80U) {
        if(*offset + 1U > dst_size) return false;
        dst[(*offset)++] = (uint8_t)value_len;
        return true;
    } else if(value_len <= 0xFFU) {
        if(*offset + 2U > dst_size) return false;
        dst[(*offset)++] = 0x81U;
        dst[(*offset)++] = (uint8_t)value_len;
        return true;
    } else if(value_len <= 0xFFFFU) {
        if(*offset + 3U > dst_size) return false;
        dst[(*offset)++] = 0x82U;
        dst[(*offset)++] = (uint8_t)(value_len >> 8U);
        dst[(*offset)++] = (uint8_t)value_len;
        return true;
    }
    return false;
}

static bool seader_uhf_snmp_append_tlv(
    uint8_t* dst,
    size_t dst_size,
    size_t* offset,
    uint8_t tag,
    const uint8_t* value,
    size_t value_len) {
    if(!dst || !offset) return false;
    if(*offset + 1U > dst_size) return false;
    dst[(*offset)++] = tag;
    if(!seader_uhf_snmp_append_len(dst, dst_size, offset, value_len)) return false;
    if(value_len > 0U) {
        if(!value || *offset + value_len > dst_size) return false;
        memcpy(dst + *offset, value, value_len);
        *offset += value_len;
    }
    return true;
}

static bool
    seader_uhf_snmp_append_uint32(uint8_t* dst, size_t dst_size, size_t* offset, uint32_t value) {
    uint8_t encoded[5] = {0};
    size_t encoded_len = 0U;

    do {
        encoded[4U - encoded_len] = (uint8_t)(value & 0xFFU);
        encoded_len++;
        value >>= 8U;
    } while(value != 0U && encoded_len < 5U);

    const uint8_t* integer_value = encoded + (5U - encoded_len);
    uint8_t prefixed[6] = {0};
    size_t prefixed_len = 0U;

    if(integer_value[0] & 0x80U) {
        prefixed[prefixed_len++] = 0x00U;
    }
    memcpy(prefixed + prefixed_len, integer_value, encoded_len);
    prefixed_len += encoded_len;

    return seader_uhf_snmp_append_tlv(dst, dst_size, offset, 0x02U, prefixed, prefixed_len);
}

static bool seader_uhf_snmp_wrap_tlv_inplace(
    uint8_t* buffer,
    size_t buffer_capacity,
    size_t value_len,
    uint8_t tag,
    size_t* encoded_len) {
    size_t header_len = 2U;

    if(!buffer || !encoded_len) return false;

    if(value_len >= 0x80U && value_len <= 0xFFU) {
        header_len = 3U;
    } else if(value_len > 0xFFU && value_len <= 0xFFFFU) {
        header_len = 4U;
    } else if(value_len > 0xFFFFU) {
        return false;
    }

    if(header_len + value_len > buffer_capacity) return false;

    memmove(buffer + header_len, buffer, value_len);
    buffer[0] = tag;

    size_t offset = 1U;
    if(!seader_uhf_snmp_append_len(buffer, buffer_capacity, &offset, value_len)) return false;

    *encoded_len = offset + value_len;
    return true;
}

static bool seader_uhf_snmp_next_tlv(
    const uint8_t* buffer,
    size_t buffer_len,
    size_t* offset,
    SeaderUhfSnmpTlv* tlv) {
    if(!buffer || !offset || !tlv || *offset + 2U > buffer_len) return false;

    tlv->tag = buffer[(*offset)++];
    uint8_t length_descriptor = buffer[(*offset)++];

    size_t value_len = 0U;
    if((length_descriptor & 0x80U) == 0U) {
        value_len = length_descriptor;
    } else {
        uint8_t length_len = (uint8_t)(length_descriptor & 0x7FU);
        if(length_len == 0U || length_len > 2U || *offset + length_len > buffer_len) return false;
        for(size_t i = 0U; i < length_len; i++) {
            value_len = (value_len << 8U) | buffer[(*offset)++];
        }
    }

    if(*offset + value_len > buffer_len) return false;
    tlv->value = buffer + *offset;
    tlv->len = value_len;
    *offset += value_len;
    return true;
}

static bool seader_uhf_snmp_parse_uint32(const uint8_t* buffer, size_t len, uint32_t* out) {
    if(!buffer || len == 0U || !out) return false;
    if(len > 1U && buffer[0] == 0x00U) {
        buffer++;
        len--;
    }
    if(len > 4U) return false;
    uint32_t value = 0U;
    for(size_t i = 0U; i < len; i++) {
        value = (value << 8U) | buffer[i];
    }
    *out = value;
    return true;
}

static bool seader_uhf_snmp_build_get_data_payload(
    const uint8_t* target_oid,
    size_t target_oid_len,
    uint32_t offset_value,
    uint32_t max_chunk_len,
    uint8_t* payload,
    size_t payload_capacity,
    size_t* payload_len) {
    if(!target_oid || target_oid_len == 0U || !payload || !payload_len) return false;
    if(!seader_uhf_snmp_append_tlv(
           payload, payload_capacity, payload_len, 0x06U, target_oid, target_oid_len))
        return false;
    if(!seader_uhf_snmp_append_uint32(payload, payload_capacity, payload_len, offset_value))
        return false;
    if(!seader_uhf_snmp_append_uint32(payload, payload_capacity, payload_len, max_chunk_len))
        return false;

    return seader_uhf_snmp_wrap_tlv_inplace(
        payload, payload_capacity, *payload_len, 0x30U, payload_len);
}

static bool seader_uhf_snmp_build_reportable_get_request(
    const uint8_t* engine_id,
    size_t engine_id_len,
    const uint8_t* user_name,
    size_t user_name_len,
    uint32_t engine_boots,
    uint32_t engine_time,
    const uint8_t* data_oid,
    size_t data_oid_len,
    const uint8_t* data,
    size_t data_len,
    uint8_t* scratch,
    size_t scratch_capacity,
    uint8_t* message,
    size_t message_capacity,
    size_t* message_len) {
    size_t scratch_len = 0U;
    size_t scoped_pdu_len = 0U;
    uint8_t global_data_value[16] = {0};
    uint8_t global_data[24] = {0};
    uint8_t version[8] = {0};
    size_t global_data_value_len = 0U;
    size_t global_data_len = 0U;
    size_t version_len = 0U;
    uint8_t flags[] = {0x04U};

    if(!scratch || !message || !message_len) return false;
    *message_len = 0U;

    if(data_oid && data_oid_len > 0U) {
        if(!seader_uhf_snmp_append_tlv(
               scratch, scratch_capacity, &scratch_len, 0x06U, data_oid, data_oid_len))
            return false;
        if(!seader_uhf_snmp_append_tlv(
               scratch, scratch_capacity, &scratch_len, 0x04U, data, data_len))
            return false;
        if(!seader_uhf_snmp_wrap_tlv_inplace(
               scratch, scratch_capacity, scratch_len, 0x30U, &scratch_len))
            return false;
    }

    memcpy(message, scratch, scratch_len);
    *message_len = scratch_len;
    if(!seader_uhf_snmp_wrap_tlv_inplace(
           message, message_capacity, *message_len, 0x30U, message_len))
        return false;

    scratch_len = 0U;
    if(!seader_uhf_snmp_append_uint32(scratch, scratch_capacity, &scratch_len, 0U) ||
       !seader_uhf_snmp_append_uint32(scratch, scratch_capacity, &scratch_len, 0U) ||
       !seader_uhf_snmp_append_uint32(scratch, scratch_capacity, &scratch_len, 0U))
        return false;
    if(scratch_len + *message_len > scratch_capacity) return false;
    memcpy(scratch + scratch_len, message, *message_len);
    scratch_len += *message_len;
    if(!seader_uhf_snmp_wrap_tlv_inplace(
           scratch, scratch_capacity, scratch_len, 0xA0U, &scratch_len))
        return false;

    *message_len = 0U;
    if(!seader_uhf_snmp_append_tlv(message, message_capacity, message_len, 0x04U, NULL, 0U) ||
       !seader_uhf_snmp_append_tlv(message, message_capacity, message_len, 0x04U, NULL, 0U))
        return false;
    if(*message_len + scratch_len > message_capacity) return false;
    memcpy(message + *message_len, scratch, scratch_len);
    *message_len += scratch_len;
    if(!seader_uhf_snmp_wrap_tlv_inplace(
           message, message_capacity, *message_len, 0x30U, &scoped_pdu_len))
        return false;

    scratch_len = 0U;
    if(!seader_uhf_snmp_append_tlv(
           scratch, scratch_capacity, &scratch_len, 0x04U, engine_id, engine_id_len) ||
       !seader_uhf_snmp_append_uint32(scratch, scratch_capacity, &scratch_len, engine_boots) ||
       !seader_uhf_snmp_append_uint32(scratch, scratch_capacity, &scratch_len, engine_time) ||
       !seader_uhf_snmp_append_tlv(
           scratch, scratch_capacity, &scratch_len, 0x04U, user_name, user_name_len) ||
       !seader_uhf_snmp_append_tlv(scratch, scratch_capacity, &scratch_len, 0x04U, NULL, 0U) ||
       !seader_uhf_snmp_append_tlv(scratch, scratch_capacity, &scratch_len, 0x04U, NULL, 0U))
        return false;
    if(!seader_uhf_snmp_wrap_tlv_inplace(
           scratch, scratch_capacity, scratch_len, 0x30U, &scratch_len) ||
       !seader_uhf_snmp_wrap_tlv_inplace(
           scratch, scratch_capacity, scratch_len, 0x04U, &scratch_len))
        return false;

    if(!seader_uhf_snmp_append_uint32(
           global_data_value, sizeof(global_data_value), &global_data_value_len, 0U) ||
       !seader_uhf_snmp_append_uint32(
           global_data_value, sizeof(global_data_value), &global_data_value_len, 756U) ||
       !seader_uhf_snmp_append_tlv(
           global_data_value,
           sizeof(global_data_value),
           &global_data_value_len,
           0x04U,
           flags,
           sizeof(flags)) ||
       !seader_uhf_snmp_append_uint32(
           global_data_value, sizeof(global_data_value), &global_data_value_len, 0x0101U)) {
        return false;
    }
    if(!seader_uhf_snmp_append_tlv(
           global_data,
           sizeof(global_data),
           &global_data_len,
           0x30U,
           global_data_value,
           global_data_value_len))
        return false;
    if(!seader_uhf_snmp_append_uint32(version, sizeof(version), &version_len, 3U)) return false;

    if(version_len + global_data_len + scratch_len + scoped_pdu_len > message_capacity)
        return false;
    memmove(message + version_len + global_data_len + scratch_len, message, scoped_pdu_len);
    memcpy(message, version, version_len);
    memcpy(message + version_len, global_data, global_data_len);
    memcpy(message + version_len + global_data_len, scratch, scratch_len);

    *message_len = version_len + global_data_len + scratch_len + scoped_pdu_len;
    return seader_uhf_snmp_wrap_tlv_inplace(
        message, message_capacity, *message_len, 0x30U, message_len);
}

bool seader_uhf_snmp_build_discovery_request(
    uint8_t* scratch,
    size_t scratch_capacity,
    uint8_t* message,
    size_t message_capacity,
    size_t* message_len) {
    return seader_uhf_snmp_build_reportable_get_request(
        NULL,
        0U,
        NULL,
        0U,
        0U,
        0U,
        NULL,
        0U,
        NULL,
        0U,
        scratch,
        scratch_capacity,
        message,
        message_capacity,
        message_len);
}

bool seader_uhf_snmp_build_get_data_request(
    const uint8_t* engine_id,
    size_t engine_id_len,
    const uint8_t* user_name,
    size_t user_name_len,
    uint32_t engine_boots,
    uint32_t engine_time,
    const uint8_t* target_oid,
    size_t target_oid_len,
    uint8_t* scratch,
    size_t scratch_capacity,
    uint8_t* message,
    size_t message_capacity,
    size_t* message_len) {
    static const uint8_t oid_get_data[] = {0x03U, 0x00U, 0x03U, 0x06U};
    size_t get_data_payload_len = 0U;

    if(!scratch || !message) return false;
    if(!seader_uhf_snmp_build_get_data_payload(
           target_oid, target_oid_len, 0U, 0x100U, message, message_capacity, &get_data_payload_len))
        return false;

    return seader_uhf_snmp_build_reportable_get_request(
        engine_id,
        engine_id_len,
        user_name,
        user_name_len,
        engine_boots,
        engine_time,
        oid_get_data,
        sizeof(oid_get_data),
        message,
        get_data_payload_len,
        scratch,
        scratch_capacity,
        message,
        message_capacity,
        message_len);
}

bool seader_uhf_snmp_parse_response(
    const uint8_t* response,
    size_t response_len,
    SeaderUhfSnmpResponseView* view) {
    size_t offset = 0U;
    SeaderUhfSnmpTlv root = {0};
    SeaderUhfSnmpTlv tlv = {0};

    if(!response || response_len == 0U || !view) return false;
    memset(view, 0, sizeof(*view));

    if(!seader_uhf_snmp_next_tlv(response, response_len, &offset, &root) || root.tag != 0x30U)
        return false;

    size_t root_offset = 0U;
    if(!seader_uhf_snmp_next_tlv(root.value, root.len, &root_offset, &tlv) || tlv.tag != 0x02U)
        return false;
    if(!seader_uhf_snmp_next_tlv(root.value, root.len, &root_offset, &tlv) || tlv.tag != 0x30U)
        return false;

    SeaderUhfSnmpTlv security_params_octet = {0};
    if(!seader_uhf_snmp_next_tlv(root.value, root.len, &root_offset, &security_params_octet) ||
       security_params_octet.tag != 0x04U)
        return false;
    if(!seader_uhf_snmp_next_tlv(root.value, root.len, &root_offset, &tlv) || tlv.tag != 0x30U)
        return false;

    size_t security_offset = 0U;
    SeaderUhfSnmpTlv security_params_seq = {0};
    if(!seader_uhf_snmp_next_tlv(
           security_params_octet.value,
           security_params_octet.len,
           &security_offset,
           &security_params_seq) ||
       security_params_seq.tag != 0x30U)
        return false;

    size_t sec_inner_offset = 0U;
    SeaderUhfSnmpTlv usm_engine_id_tlv = {0};
    SeaderUhfSnmpTlv usm_engine_boots_tlv = {0};
    SeaderUhfSnmpTlv usm_engine_time_tlv = {0};
    SeaderUhfSnmpTlv usm_username_tlv = {0};
    SeaderUhfSnmpTlv ignore = {0};

    if(!seader_uhf_snmp_next_tlv(
           security_params_seq.value,
           security_params_seq.len,
           &sec_inner_offset,
           &usm_engine_id_tlv) ||
       usm_engine_id_tlv.tag != 0x04U)
        return false;
    if(!seader_uhf_snmp_next_tlv(
           security_params_seq.value,
           security_params_seq.len,
           &sec_inner_offset,
           &usm_engine_boots_tlv) ||
       usm_engine_boots_tlv.tag != 0x02U)
        return false;
    if(!seader_uhf_snmp_next_tlv(
           security_params_seq.value,
           security_params_seq.len,
           &sec_inner_offset,
           &usm_engine_time_tlv) ||
       usm_engine_time_tlv.tag != 0x02U)
        return false;
    if(!seader_uhf_snmp_next_tlv(
           security_params_seq.value,
           security_params_seq.len,
           &sec_inner_offset,
           &usm_username_tlv) ||
       usm_username_tlv.tag != 0x04U)
        return false;
    if(!seader_uhf_snmp_next_tlv(
           security_params_seq.value, security_params_seq.len, &sec_inner_offset, &ignore) ||
       ignore.tag != 0x04U)
        return false;
    if(!seader_uhf_snmp_next_tlv(
           security_params_seq.value, security_params_seq.len, &sec_inner_offset, &ignore) ||
       ignore.tag != 0x04U)
        return false;

    size_t scoped_offset = 0U;
    SeaderUhfSnmpTlv context_engine_tlv = {0};
    SeaderUhfSnmpTlv context_name_tlv = {0};
    SeaderUhfSnmpTlv pdu_tlv = {0};
    if(!seader_uhf_snmp_next_tlv(tlv.value, tlv.len, &scoped_offset, &context_engine_tlv) ||
       context_engine_tlv.tag != 0x04U)
        return false;
    if(!seader_uhf_snmp_next_tlv(tlv.value, tlv.len, &scoped_offset, &context_name_tlv) ||
       context_name_tlv.tag != 0x04U)
        return false;
    if(!seader_uhf_snmp_next_tlv(tlv.value, tlv.len, &scoped_offset, &pdu_tlv)) return false;

    size_t pdu_offset = 0U;
    SeaderUhfSnmpTlv req_id_tlv = {0};
    SeaderUhfSnmpTlv error_status_tlv = {0};
    SeaderUhfSnmpTlv error_index_tlv = {0};
    SeaderUhfSnmpTlv varbinds_tlv = {0};
    if(!seader_uhf_snmp_next_tlv(pdu_tlv.value, pdu_tlv.len, &pdu_offset, &req_id_tlv) ||
       req_id_tlv.tag != 0x02U)
        return false;
    if(!seader_uhf_snmp_next_tlv(pdu_tlv.value, pdu_tlv.len, &pdu_offset, &error_status_tlv) ||
       error_status_tlv.tag != 0x02U)
        return false;
    if(!seader_uhf_snmp_next_tlv(pdu_tlv.value, pdu_tlv.len, &pdu_offset, &error_index_tlv) ||
       error_index_tlv.tag != 0x02U)
        return false;
    if(!seader_uhf_snmp_next_tlv(pdu_tlv.value, pdu_tlv.len, &pdu_offset, &varbinds_tlv) ||
       varbinds_tlv.tag != 0x30U)
        return false;

    view->context_engine_id = context_engine_tlv.value;
    view->context_engine_id_len = context_engine_tlv.len;
    view->usm_engine_id = usm_engine_id_tlv.value;
    view->usm_engine_id_len = usm_engine_id_tlv.len;
    view->usm_username = usm_username_tlv.value;
    view->usm_username_len = usm_username_tlv.len;
    view->varbind_sequence = varbinds_tlv.value;
    view->varbind_sequence_len = varbinds_tlv.len;

    if(!seader_uhf_snmp_parse_uint32(
           usm_engine_boots_tlv.value, usm_engine_boots_tlv.len, &view->usm_engine_boots) ||
       !seader_uhf_snmp_parse_uint32(
           usm_engine_time_tlv.value, usm_engine_time_tlv.len, &view->usm_engine_time) ||
       !seader_uhf_snmp_parse_uint32(
           error_status_tlv.value, error_status_tlv.len, &view->error_status))
        return false;

    return true;
}

bool seader_uhf_snmp_find_varbind_octet_value(
    const uint8_t* varbind_sequence,
    size_t varbind_sequence_len,
    const uint8_t* expected_oid,
    size_t expected_oid_len,
    const uint8_t** value,
    size_t* value_len) {
    size_t offset = 0U;

    if(!varbind_sequence || !expected_oid || expected_oid_len == 0U) return false;
    while(offset < varbind_sequence_len) {
        SeaderUhfSnmpTlv varbind_tlv = {0};
        SeaderUhfSnmpTlv oid_tlv = {0};
        SeaderUhfSnmpTlv value_tlv = {0};
        size_t varbind_offset = 0U;

        if(!seader_uhf_snmp_next_tlv(
               varbind_sequence, varbind_sequence_len, &offset, &varbind_tlv) ||
           varbind_tlv.tag != 0x30U)
            return false;
        if(!seader_uhf_snmp_next_tlv(
               varbind_tlv.value, varbind_tlv.len, &varbind_offset, &oid_tlv) ||
           oid_tlv.tag != 0x06U)
            return false;
        if(!seader_uhf_snmp_next_tlv(
               varbind_tlv.value, varbind_tlv.len, &varbind_offset, &value_tlv))
            return false;

        if(oid_tlv.len == expected_oid_len &&
           memcmp(oid_tlv.value, expected_oid, expected_oid_len) == 0U) {
            if(value_tlv.tag != 0x04U) return false;
            if(value) *value = value_tlv.value;
            if(value_len) *value_len = value_tlv.len;
            return true;
        }
    }

    return false;
}
