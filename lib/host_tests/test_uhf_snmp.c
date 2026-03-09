#include <ctype.h>
#include <string.h>

#include "munit.h"
#include "uhf_snmp.h"

static const char* snmp_discovery_request_hex =
    "3038020103300E020100020202F4040104020201010410300E0400020100020100040004000400301104000400A00B0201000201000201003000";

static const char* snmp_discovery_response_hex =
    "308200A80201033082000E02010002010004020000020201010482003A30820036041B2B0601040181E438010103050F8C9088CDA2A8D885C0B298D7FF7F020105020101040D2B0601040181E438010104080F0400040030820051041B2B0601040181E438010103050F8C9088CDA2A8D885C0B298D7FF7F040D2B0601040181E438010104080FA08200210201000201000201003082001430820010060A2B060106030F010104000202013E";

static const char* snmp_ice_response_hex =
    "308200A80201033082000E02010002010004020000020201010482003A30820036041B2B0601040181E438010103050F8C9088CDA2A8D885C0B298D7FF7F020105020102040D2B0601040181E438010104080F0400040030820051041B2B0601040181E438010103050F8C9088CDA2A8D885C0B298D7FF7F040D2B0601040181E438010104080FA2820021020100020100020100308200143082001006050301070138040749434531383033";

static const char* snmp_uhf_config_response_hex =
    "308200F40201033082000E02010002010004020000020201010482003A30820036041B2B0601040181E438010103050F8C9088CDA2A8D885C0B298D7FF7F020105020103040D2B0601040181E438010104080F040004003082009D041B2B0601040181E438010103050F8C9088CDA2A8D885C0B298D7FF7F040D2B0601040181E438010104080FA282006D020100020100020100308200603082005C0606030107030B00045204E2003412112B0601040181E438010102012201010101112B0601040181E43801010201220101020104E2801105112B0601040181E438010102011E01010101112B0601040181E438010102011E01010201";

static const uint8_t oid_elite_ice[] = {0x03, 0x01, 0x07, 0x01, 0x38};
static const uint8_t oid_uhf_tags_config[] = {0x03, 0x01, 0x07, 0x03, 0x0B, 0x00};

static size_t test_hex_to_bytes(const char* hex, uint8_t* out, size_t out_size) {
    size_t len = 0U;
    int high_nibble = -1;

    for(const char* p = hex; *p; ++p) {
        int value = -1;
        if(*p >= '0' && *p <= '9') {
            value = *p - '0';
        } else if(*p >= 'A' && *p <= 'F') {
            value = *p - 'A' + 10;
        } else if(*p >= 'a' && *p <= 'f') {
            value = *p - 'a' + 10;
        } else if(isspace((unsigned char)*p)) {
            continue;
        } else {
            munit_error("invalid hex character");
        }

        if(high_nibble < 0) {
            high_nibble = value;
        } else {
            if(len >= out_size) {
                munit_error("hex output buffer too small");
            }
            out[len++] = (uint8_t)((high_nibble << 4) | value);
            high_nibble = -1;
        }
    }

    if(high_nibble >= 0) {
        munit_error("odd-length hex string");
    }

    return len;
}

static MunitResult test_build_discovery_request_matches_live_vector(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    uint8_t message[SEADER_SNMP_MAX_MSG_LEN] = {0};
    uint8_t expected[SEADER_SNMP_MAX_MSG_LEN] = {0};
    size_t message_len = 0U;
    size_t expected_len = test_hex_to_bytes(snmp_discovery_request_hex, expected, sizeof(expected));

    munit_assert_true(
        seader_uhf_snmp_build_discovery_request(message, sizeof(message), &message_len));
    munit_assert_size(message_len, ==, expected_len);
    munit_assert_memory_equal(expected_len, message, expected);
    return MUNIT_OK;
}

static MunitResult test_parse_discovery_response_extracts_usm_context(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    static const uint8_t expected_engine_id[] = {
        0x2B, 0x06, 0x01, 0x04, 0x01, 0x81, 0xE4, 0x38, 0x01, 0x01, 0x03,
        0x05, 0x0F, 0x8C, 0x90, 0x88, 0xCD, 0xA2, 0xA8, 0xD8, 0x85, 0xC0,
        0xB2, 0x98, 0xD7, 0xFF, 0x7F};
    static const uint8_t expected_username[] = {
        0x2B, 0x06, 0x01, 0x04, 0x01, 0x81, 0xE4, 0x38, 0x01, 0x01, 0x04, 0x08, 0x0F};
    uint8_t response[256] = {0};
    size_t response_len =
        test_hex_to_bytes(snmp_discovery_response_hex, response, sizeof(response));
    SeaderUhfSnmpResponseView view = {0};

    munit_assert_true(seader_uhf_snmp_parse_response(response, response_len, &view));
    munit_assert_uint32(view.error_status, ==, 0U);
    munit_assert_uint32(view.usm_engine_boots, ==, 5U);
    munit_assert_uint32(view.usm_engine_time, ==, 1U);
    munit_assert_size(view.context_engine_id_len, ==, sizeof(expected_engine_id));
    munit_assert_size(view.usm_engine_id_len, ==, sizeof(expected_engine_id));
    munit_assert_size(view.usm_username_len, ==, sizeof(expected_username));
    munit_assert_memory_equal(
        sizeof(expected_engine_id), view.context_engine_id, expected_engine_id);
    munit_assert_memory_equal(sizeof(expected_engine_id), view.usm_engine_id, expected_engine_id);
    munit_assert_memory_equal(sizeof(expected_username), view.usm_username, expected_username);
    return MUNIT_OK;
}

static MunitResult test_parse_ice_response_extracts_value(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    static const uint8_t expected[] = {'I', 'C', 'E', '1', '8', '0', '3'};
    uint8_t response[256] = {0};
    size_t response_len = test_hex_to_bytes(snmp_ice_response_hex, response, sizeof(response));
    SeaderUhfSnmpResponseView view = {0};
    const uint8_t* value = NULL;
    size_t value_len = 0U;

    munit_assert_true(seader_uhf_snmp_parse_response(response, response_len, &view));
    munit_assert_true(seader_uhf_snmp_find_varbind_octet_value(
        view.varbind_sequence,
        view.varbind_sequence_len,
        oid_elite_ice,
        sizeof(oid_elite_ice),
        &value,
        &value_len));
    munit_assert_size(value_len, ==, sizeof(expected));
    munit_assert_memory_equal(sizeof(expected), value, expected);
    return MUNIT_OK;
}

static MunitResult test_parse_uhf_config_response_extracts_value(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    static const uint8_t expected[] = {
        0x04, 0xE2, 0x00, 0x34, 0x12, 0x11, 0x2B, 0x06, 0x01, 0x04, 0x01, 0x81,
        0xE4, 0x38, 0x01, 0x01, 0x02, 0x01, 0x22, 0x01, 0x01, 0x01, 0x01, 0x11,
        0x2B, 0x06, 0x01, 0x04, 0x01, 0x81, 0xE4, 0x38, 0x01, 0x01, 0x02, 0x01,
        0x22, 0x01, 0x01, 0x02, 0x01, 0x04, 0xE2, 0x80, 0x11, 0x05, 0x11, 0x2B,
        0x06, 0x01, 0x04, 0x01, 0x81, 0xE4, 0x38, 0x01, 0x01, 0x02, 0x01, 0x1E,
        0x01, 0x01, 0x01, 0x01, 0x11, 0x2B, 0x06, 0x01, 0x04, 0x01, 0x81, 0xE4,
        0x38, 0x01, 0x01, 0x02, 0x01, 0x1E, 0x01, 0x01, 0x02, 0x01};
    uint8_t response[384] = {0};
    size_t response_len =
        test_hex_to_bytes(snmp_uhf_config_response_hex, response, sizeof(response));
    SeaderUhfSnmpResponseView view = {0};
    const uint8_t* value = NULL;
    size_t value_len = 0U;

    munit_assert_true(seader_uhf_snmp_parse_response(response, response_len, &view));
    munit_assert_true(seader_uhf_snmp_find_varbind_octet_value(
        view.varbind_sequence,
        view.varbind_sequence_len,
        oid_uhf_tags_config,
        sizeof(oid_uhf_tags_config),
        &value,
        &value_len));
    munit_assert_size(value_len, ==, sizeof(expected));
    munit_assert_memory_equal(sizeof(expected), value, expected);
    return MUNIT_OK;
}

static MunitTest test_uhf_snmp_cases[] = {
    {(char*)"/build-discovery", test_build_discovery_request_matches_live_vector, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/parse-discovery", test_parse_discovery_response_extracts_usm_context, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/parse-ice", test_parse_ice_response_extracts_value, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/parse-uhf-config", test_parse_uhf_config_response_extracts_value, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {NULL, NULL, NULL, NULL, 0, NULL},
};

MunitSuite test_uhf_snmp_suite = {
    "",
    test_uhf_snmp_cases,
    NULL,
    1,
    MUNIT_SUITE_OPTION_NONE,
};
