#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SEADER_SNMP_MAX_MSG_LEN 240U
#define SEADER_SNMP_MAX_ID_LEN  32U

typedef struct {
    const uint8_t* context_engine_id;
    size_t context_engine_id_len;
    const uint8_t* usm_engine_id;
    size_t usm_engine_id_len;
    uint32_t usm_engine_boots;
    uint32_t usm_engine_time;
    const uint8_t* usm_username;
    size_t usm_username_len;
    const uint8_t* varbind_sequence;
    size_t varbind_sequence_len;
    uint32_t error_status;
} SeaderUhfSnmpResponseView;

bool seader_uhf_snmp_build_discovery_request(
    uint8_t* scratch,
    size_t scratch_capacity,
    uint8_t* message,
    size_t message_capacity,
    size_t* message_len);

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
    size_t* message_len);

bool seader_uhf_snmp_parse_response(
    const uint8_t* response,
    size_t response_len,
    SeaderUhfSnmpResponseView* view);

bool seader_uhf_snmp_find_varbind_octet_value(
    const uint8_t* varbind_sequence,
    size_t varbind_sequence_len,
    const uint8_t* expected_oid,
    size_t expected_oid_len,
    const uint8_t** value,
    size_t* value_len);
