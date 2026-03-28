#include <ctype.h>
#include <string.h>

#include "munit.h"
#include "uhf_logic.h"
#include "uhf_routed.h"

static size_t test_hex_to_bytes(const char* hex, uint8_t* out, size_t out_size) {
    size_t len = 0U;
    int high_nibble = -1;

    for(const char* p = hex; *p; ++p) {
        int value = -1;
        if(*p >= '0' && *p <= '9') value = *p - '0';
        else if(*p >= 'A' && *p <= 'F') value = *p - 'A' + 10;
        else if(*p >= 'a' && *p <= 'f') value = *p - 'a' + 10;
        else if(isspace((unsigned char)*p)) continue;
        else munit_error("invalid hex character");

        if(high_nibble < 0) {
            high_nibble = value;
        } else {
            if(len >= out_size) munit_error("hex output buffer too small");
            out[len++] = (uint8_t)((high_nibble << 4) | value);
            high_nibble = -1;
        }
    }

    if(high_nibble >= 0) munit_error("odd-length hex string");
    return len;
}

static MunitResult test_fake_trace_card_detected_uses_normalized_csn(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    const char* card_detected_request_hex =
        "440A44000000A01AAD18A016800200148110E28011050000723800007AD491520AD1";
    const uint8_t expected_csn[] = {0xE2, 0x80, 0x11, 0x05, 0x00, 0x00, 0x72, 0x38,
                                    0x00, 0x00, 0x7A, 0xD4, 0x91, 0x52, 0x0A, 0xD1};
    const uint8_t public_epc[] = {0x00, 0x00, 0x72, 0x38, 0x00, 0x00,
                                  0x7A, 0xD4, 0x91, 0x52, 0x0A, 0xD1};
    const uint8_t private_form[] = {0xE2, 0x80, 0x11, 0x05, 0x20, 0x00,
                                    0x7A, 0xD4, 0x91, 0x52, 0x0A, 0xD1};
    uint8_t actual[SEADER_UHF_NORMALIZED_CSN_LEN] = {0};
    size_t actual_len = 0U;
    uint8_t request[128] = {0};
    size_t request_len = test_hex_to_bytes(card_detected_request_hex, request, sizeof(request));

    munit_assert_true(seader_uhf_build_sam_csn_from_form(
        public_epc, sizeof(public_epc), actual, sizeof(actual), &actual_len));
    munit_assert_size(actual_len, ==, sizeof(expected_csn));
    munit_assert_memory_equal(sizeof(expected_csn), actual, expected_csn);

    memset(actual, 0, sizeof(actual));
    actual_len = 0U;
    munit_assert_true(seader_uhf_build_sam_csn_from_form(
        private_form, sizeof(private_form), actual, sizeof(actual), &actual_len));
    munit_assert_size(actual_len, ==, sizeof(expected_csn));
    munit_assert_memory_equal(sizeof(expected_csn), actual, expected_csn);

    munit_assert_size(request_len, >, sizeof(expected_csn));
    munit_assert_memory_equal(
        sizeof(expected_csn), request + request_len - sizeof(expected_csn), expected_csn);
    return MUNIT_OK;
}

static MunitResult test_fake_trace_replays_routed_command_sequence(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    const char* routed_get_properties_hex =
        "0A540A000000AA82001004060A2E0A0000000202002EB6028400";
    const char* routed_set_access_password_hex =
        "0A540A000000AA82001404060A2E0A0000000202002EB60286049619E8D3";
    const uint8_t expected_password[] = {0x96, 0x19, 0xE8, 0xD3};
    uint8_t apdu[64] = {0};
    size_t apdu_len = 0U;
    SeaderUhfRoutedFrame frame = {0};

    apdu_len = test_hex_to_bytes(routed_get_properties_hex, apdu, sizeof(apdu));
    munit_assert_true(seader_uhf_routed_try_parse_apdu(apdu, apdu_len, &frame));
    munit_assert_int(frame.command, ==, SeaderUhfRoutedCommandGetProperties);
    munit_assert_uint32(frame.bus_address, ==, 0x2eU);
    munit_assert_memory_equal(6, frame.route_header, ((const uint8_t*)"\x0a\x54\x0a\x00\x00\x00"));
    munit_assert_memory_equal(6, frame.i2c_header, ((const uint8_t*)"\x0a\x2e\x0a\x00\x00\x00"));

    memset(&frame, 0, sizeof(frame));
    apdu_len = test_hex_to_bytes(routed_set_access_password_hex, apdu, sizeof(apdu));
    munit_assert_true(seader_uhf_routed_try_parse_apdu(apdu, apdu_len, &frame));
    munit_assert_int(frame.command, ==, SeaderUhfRoutedCommandSetAccessPassword);
    munit_assert_size(frame.command_value_len, ==, sizeof(expected_password));
    munit_assert_memory_equal(sizeof(expected_password), frame.command_value, expected_password);
    return MUNIT_OK;
}

static MunitResult test_synthetic_get_private_data_from_u90_shape(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    const char* routed_get_private_data_hex =
        "0A540A000000AA82001004060A2E0A0000000202002EB6028700";
    const uint8_t private_tid[] = {0xE2, 0x80, 0x11, 0x05, 0x20, 0x00,
                                   0x7A, 0xD4, 0x91, 0x52, 0x0A, 0xD1};
    const uint8_t sio[] = {
        0x30, 0x35, 0x81, 0x05, 0x01, 0x8C, 0xE0, 0x50, 0x01, 0xA5, 0x02, 0x05,
        0x00, 0xA6, 0x08, 0x81, 0x01, 0x01, 0x04, 0x03, 0x03, 0x00, 0x08, 0xA7,
        0x1A, 0x85, 0x18, 0x35, 0x60, 0x25, 0xDA, 0xEC, 0x61, 0x93, 0xBE, 0x30,
        0xB0, 0x24, 0x05, 0x90, 0x0E, 0x87, 0x34, 0xB1, 0x6B, 0x0E, 0x8E, 0x45,
        0x68, 0x3A, 0x5B, 0xA9, 0x02, 0x05, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00};
    uint8_t apdu[64] = {0};
    size_t apdu_len = test_hex_to_bytes(routed_get_private_data_hex, apdu, sizeof(apdu));
    SeaderUhfRoutedFrame frame = {0};
    uint8_t tid_out[SEADER_UHF_TRACE_TID_LEN] = {0};
    uint8_t user_data_out[SEADER_UHF_SAM_USER_DATA_LEN] = {0};
    size_t tid_out_len = 0U;
    size_t user_data_out_len = 0U;

    munit_assert_true(seader_uhf_routed_try_parse_apdu(apdu, apdu_len, &frame));
    munit_assert_int(frame.command, ==, SeaderUhfRoutedCommandGetPrivateData);

    munit_assert_true(seader_uhf_normalize_private_data_for_sam(
        private_tid,
        sizeof(private_tid),
        sio,
        sizeof(sio),
        tid_out,
        sizeof(tid_out),
        &tid_out_len,
        user_data_out,
        sizeof(user_data_out),
        &user_data_out_len));
    munit_assert_size(tid_out_len, ==, sizeof(private_tid));
    munit_assert_memory_equal(sizeof(private_tid), tid_out, private_tid);
    munit_assert_size(user_data_out_len, ==, SEADER_UHF_SAM_USER_DATA_LEN);
    munit_assert_memory_equal(sizeof(sio), user_data_out, sio);
    for(size_t i = sizeof(sio); i < user_data_out_len; i++) {
        munit_assert_uint8(user_data_out[i], ==, 0U);
    }

    return MUNIT_OK;
}

static MunitResult test_live_public_unlock_relock_vectors(const MunitParameter params[], void* fixture) {
    (void)params;
    (void)fixture;

    const uint8_t public_epc[] = {0x00, 0x00, 0x72, 0x38, 0x00, 0x00,
                                  0x7A, 0xD4, 0x91, 0x52, 0x0A, 0xD1};
    const uint8_t expected_private_epc[] = {
        0x31, 0x31, 0x31, 0x30, 0x32, 0x38, 0x37, 0x39, 0x33, 0x30, 0x37, 0x2D};
    const uint8_t expected_private_tid[] = {
        0xE2, 0x80, 0x11, 0x05, 0x20, 0x00, 0x7A, 0xD4, 0x91, 0x52, 0x0A, 0xD1};
    uint8_t actual_private_tid[SEADER_UHF_TRACE_TID_LEN] = {0};
    size_t actual_private_tid_len = 0U;

    munit_assert_true(seader_uhf_is_public_epc_form(public_epc, sizeof(public_epc)));
    munit_assert_true(seader_uhf_build_private_tid_from_form(
        public_epc,
        sizeof(public_epc),
        actual_private_tid,
        sizeof(actual_private_tid),
        &actual_private_tid_len));
    munit_assert_size(actual_private_tid_len, ==, sizeof(expected_private_tid));
    munit_assert_memory_equal(sizeof(expected_private_tid), actual_private_tid, expected_private_tid);
    munit_assert_false(seader_uhf_is_public_epc_form(expected_private_epc, sizeof(expected_private_epc)));
    return MUNIT_OK;
}

static MunitTest test_fake_uhf_trace_cases[] = {
    {(char*)"/card-detected-csn", test_fake_trace_card_detected_uses_normalized_csn, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/routed-sequence", test_fake_trace_replays_routed_command_sequence, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/synthetic-get-private-data", test_synthetic_get_private_data_from_u90_shape, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/live-public-unlock-relock", test_live_public_unlock_relock_vectors, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {NULL, NULL, NULL, NULL, 0, NULL},
};

MunitSuite test_fake_uhf_trace_suite = {
    "",
    test_fake_uhf_trace_cases,
    NULL,
    1,
    MUNIT_SUITE_OPTION_NONE,
};
