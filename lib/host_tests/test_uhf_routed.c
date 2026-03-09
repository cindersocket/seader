#include "munit.h"
#include "uhf_routed.h"

static MunitResult test_parse_get_properties_trace(const MunitParameter params[], void* fixture) {
    (void)params;
    (void)fixture;

    const uint8_t apdu[] = {
        0x0a, 0x54, 0x0a, 0x00, 0x00, 0x00, 0xaa, 0x82, 0x00, 0x10, 0x04, 0x06, 0x0a,
        0x2e, 0x0a, 0x00, 0x00, 0x00, 0x02, 0x02, 0x00, 0x2e, 0xb6, 0x02, 0x84, 0x00};
    SeaderUhfRoutedFrame frame = {0};

    munit_assert_true(seader_uhf_routed_try_parse_apdu(apdu, sizeof(apdu), &frame));
    munit_assert_uint32(frame.bus_address, ==, 0x2eU);
    munit_assert_int(frame.command, ==, SeaderUhfRoutedCommandGetProperties);
    munit_assert_size(frame.command_value_len, ==, 0U);
    munit_assert_memory_equal(6, frame.i2c_header, ((const uint8_t*)"\x0a\x2e\x0a\x00\x00\x00"));
    return MUNIT_OK;
}

static MunitResult test_parse_set_access_password_trace(const MunitParameter params[], void* fixture) {
    (void)params;
    (void)fixture;

    const uint8_t apdu[] = {
        0x0a, 0x54, 0x0a, 0x00, 0x00, 0x00, 0xaa, 0x82, 0x00, 0x14, 0x04, 0x06, 0x0a,
        0x2e, 0x0a, 0x00, 0x00, 0x00, 0x02, 0x02, 0x00, 0x2e, 0xb6, 0x02, 0x86, 0x04,
        0x1e, 0xe7, 0xdc, 0x68};
    const uint8_t expected_password[] = {0x1e, 0xe7, 0xdc, 0x68};
    SeaderUhfRoutedFrame frame = {0};

    munit_assert_true(seader_uhf_routed_try_parse_apdu(apdu, sizeof(apdu), &frame));
    munit_assert_int(frame.command, ==, SeaderUhfRoutedCommandSetAccessPassword);
    munit_assert_size(frame.command_value_len, ==, sizeof(expected_password));
    munit_assert_memory_equal(sizeof(expected_password), frame.command_value, expected_password);
    return MUNIT_OK;
}

static MunitResult test_parse_get_private_data_trace(const MunitParameter params[], void* fixture) {
    (void)params;
    (void)fixture;

    const uint8_t apdu[] = {
        0x0a, 0x54, 0x0a, 0x00, 0x00, 0x00, 0xaa, 0x82, 0x00, 0x10, 0x04, 0x06, 0x0a,
        0x2e, 0x0a, 0x00, 0x00, 0x00, 0x02, 0x02, 0x00, 0x2e, 0xb6, 0x02, 0x87, 0x00};
    SeaderUhfRoutedFrame frame = {0};

    munit_assert_true(seader_uhf_routed_try_parse_apdu(apdu, sizeof(apdu), &frame));
    munit_assert_int(frame.command, ==, SeaderUhfRoutedCommandGetPrivateData);
    munit_assert_size(frame.command_value_len, ==, 0U);
    return MUNIT_OK;
}

static MunitResult test_reject_non_routed_payload(const MunitParameter params[], void* fixture) {
    (void)params;
    (void)fixture;

    const uint8_t apdu[] = {0x0a, 0x44, 0x00, 0x00, 0x00, 0x00, 0xbd, 0x02, 0x8a, 0x00};
    SeaderUhfRoutedFrame frame = {0};

    munit_assert_false(seader_uhf_routed_try_parse_apdu(apdu, sizeof(apdu), &frame));
    return MUNIT_OK;
}

static MunitTest test_uhf_routed_cases[] = {
    {(char*)"/parse-get-properties", test_parse_get_properties_trace, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/parse-set-access-password", test_parse_set_access_password_trace, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/parse-get-private-data", test_parse_get_private_data_trace, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/reject-non-routed", test_reject_non_routed_payload, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {NULL, NULL, NULL, NULL, 0, NULL},
};

MunitSuite test_uhf_routed_suite = {
    "",
    test_uhf_routed_cases,
    NULL,
    1,
    MUNIT_SUITE_OPTION_NONE,
};
