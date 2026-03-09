#include <string.h>

#include "munit.h"
#include "sam_apdu.h"

static MunitResult test_extended_put_data_places_le_after_payload(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    const uint8_t payload[] = {0x44, 0x0A, 0x44, 0x00, 0x00, 0x00, 0xA0, 0x02, 0x82, 0x00};
    const uint8_t expected[] = {
        0xA0, 0xDA, 0x02, 0x63, 0x00, 0x00, 0x0A, 0x44, 0x0A,
        0x44, 0x00, 0x00, 0x00, 0xA0, 0x02, 0x82, 0x00, 0x00, 0x00};
    uint8_t apdu[32] = {0};
    size_t apdu_len = 0U;

    munit_assert_true(seader_build_command_apdu(
        true,
        true,
        0xA0,
        0xDA,
        0x02,
        0x63,
        payload,
        sizeof(payload),
        apdu,
        sizeof(apdu),
        &apdu_len));
    munit_assert_size(apdu_len, ==, sizeof(expected));
    munit_assert_memory_equal(apdu_len, apdu, expected);
    return MUNIT_OK;
}

static MunitResult test_extended_apdu_without_explicit_le(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    const uint8_t payload[] = {0x01, 0x02, 0x03};
    const uint8_t expected[] = {0x00, 0xCA, 0x00, 0x00, 0x00, 0x00, 0x03, 0x01, 0x02, 0x03};
    uint8_t apdu[16] = {0};
    size_t apdu_len = 0U;

    munit_assert_true(seader_build_command_apdu(
        true, false, 0x00, 0xCA, 0x00, 0x00, payload, sizeof(payload), apdu, sizeof(apdu), &apdu_len));
    munit_assert_size(apdu_len, ==, sizeof(expected));
    munit_assert_memory_equal(apdu_len, apdu, expected);
    return MUNIT_OK;
}

static MunitResult test_extended_put_data_inplace_scratchpad(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    uint8_t buffer[32] = {0};
    const uint8_t payload[] = {0x44, 0x0A, 0x44, 0x00, 0x00, 0x00, 0xA0, 0x02, 0x82, 0x00};
    const uint8_t expected[] = {
        0xA0, 0xDA, 0x02, 0x63, 0x00, 0x00, 0x0A, 0x44, 0x0A,
        0x44, 0x00, 0x00, 0x00, 0xA0, 0x02, 0x82, 0x00, 0x00, 0x00};
    size_t apdu_len = 0U;

    memcpy(buffer + 7U, payload, sizeof(payload));
    munit_assert_true(seader_build_command_apdu_inplace(
        true, true, 0xA0, 0xDA, 0x02, 0x63, buffer, sizeof(buffer), 7U, sizeof(payload), &apdu_len));
    munit_assert_size(apdu_len, ==, sizeof(expected));
    munit_assert_memory_equal(apdu_len, buffer, expected);
    return MUNIT_OK;
}

static MunitTest test_sam_apdu_cases[] = {
    {(char*)"/extended-put-data-le-after-payload",
     test_extended_put_data_places_le_after_payload,
     NULL,
     NULL,
     MUNIT_TEST_OPTION_NONE,
     NULL},
    {(char*)"/extended-no-explicit-le",
     test_extended_apdu_without_explicit_le,
     NULL,
     NULL,
     MUNIT_TEST_OPTION_NONE,
     NULL},
    {(char*)"/extended-put-data-inplace-scratchpad",
     test_extended_put_data_inplace_scratchpad,
     NULL,
     NULL,
     MUNIT_TEST_OPTION_NONE,
     NULL},
    {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
};

const MunitSuite test_sam_apdu_suite = {
    "/sam-apdu",
    test_sam_apdu_cases,
    NULL,
    1,
    MUNIT_SUITE_OPTION_NONE,
};
