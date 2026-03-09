#include <string.h>

#include "munit.h"
#include "uhf_frame.h"

static MunitResult test_build_hw_version_probe_frame(const MunitParameter params[], void* fixture) {
    (void)params;
    (void)fixture;

    uint8_t frame[16] = {0};
    size_t frame_len = 0U;
    const uint8_t expected[] = {0xBB, 0x00, 0x03, 0x00, 0x01, 0x00, 0x04, 0x7E};

    munit_assert_true(
        seader_uhf_build_frame(0x03U, (const uint8_t[]){0x00U}, 1U, frame, sizeof(frame), &frame_len));
    munit_assert_size(frame_len, ==, sizeof(expected));
    munit_assert_memory_equal(sizeof(expected), frame, expected);
    return MUNIT_OK;
}

static MunitResult test_parse_module_present_probe_response(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    const uint8_t response[] = {
        0xBB, 0x01, 0x03, 0x00, 0x10, 0x00, 0x4D, 0x31, 0x30, 0x30, 0x20, 0x32,
        0x36, 0x64, 0x42, 0x6D, 0x20, 0x56, 0x31, 0x2E, 0x30, 0x92, 0x7E};
    uint8_t payload[32] = {0};
    size_t payload_len = 0U;
    const uint8_t expected_payload[] = {
        0x00, 0x4D, 0x31, 0x30, 0x30, 0x20, 0x32, 0x36,
        0x64, 0x42, 0x6D, 0x20, 0x56, 0x31, 0x2E, 0x30};

    munit_assert_true(seader_uhf_try_parse_frame(
        response, sizeof(response), 0x01U, 0x03U, payload, sizeof(payload), &payload_len));
    munit_assert_size(payload_len, ==, sizeof(expected_payload));
    munit_assert_memory_equal(sizeof(expected_payload), payload, expected_payload);
    return MUNIT_OK;
}

static MunitResult test_parse_module_missing_probe_response(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    uint8_t payload[8] = {0};
    size_t payload_len = 0U;

    munit_assert_false(
        seader_uhf_try_parse_frame(NULL, 0U, 0x01U, 0x03U, payload, sizeof(payload), &payload_len));
    return MUNIT_OK;
}

static MunitResult test_reject_bad_checksum_probe_response(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    const uint8_t response[] = {
        0xBB, 0x01, 0x03, 0x00, 0x10, 0x00, 0x4D, 0x31, 0x30, 0x30, 0x20, 0x32,
        0x36, 0x64, 0x42, 0x6D, 0x20, 0x56, 0x31, 0x2E, 0x30, 0x93, 0x7E};
    uint8_t payload[32] = {0};
    size_t payload_len = 0U;

    munit_assert_false(seader_uhf_try_parse_frame(
        response, sizeof(response), 0x01U, 0x03U, payload, sizeof(payload), &payload_len));
    return MUNIT_OK;
}

static MunitTest test_uhf_frame_cases[] = {
    {(char*)"/build-hw-version", test_build_hw_version_probe_frame, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/parse-present", test_parse_module_present_probe_response, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/parse-missing", test_parse_module_missing_probe_response, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/reject-bad-checksum", test_reject_bad_checksum_probe_response, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {NULL, NULL, NULL, NULL, 0, NULL},
};

MunitSuite test_uhf_frame_suite = {
    "",
    test_uhf_frame_cases,
    NULL,
    1,
    MUNIT_SUITE_OPTION_NONE,
};
