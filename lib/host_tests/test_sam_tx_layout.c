#include "munit.h"

#include "sam_tx_layout.h"

static MunitResult test_headroom_constants(const MunitParameter params[], void* fixture) {
    (void)params;
    (void)fixture;

    munit_assert_size(SEADER_SAM_CCID_TX_HEADROOM, ==, 12U);
    munit_assert_size(SEADER_SAM_T1_TX_HEADROOM, ==, 3U);
    munit_assert_size(SEADER_SAM_APDU_TX_HEADROOM, ==, 15U);
    return MUNIT_OK;
}

static MunitResult test_payload_offsets(const MunitParameter params[], void* fixture) {
    (void)params;
    (void)fixture;

    munit_assert_size(seader_sam_apdu_header_len(false), ==, 5U);
    munit_assert_size(seader_sam_apdu_header_len(true), ==, 7U);
    munit_assert_size(seader_sam_payload_offset(false), ==, 20U);
    munit_assert_size(seader_sam_payload_offset(true), ==, 22U);
    return MUNIT_OK;
}

static MunitResult test_payload_capacity(const MunitParameter params[], void* fixture) {
    (void)params;
    (void)fixture;

    munit_assert_size(seader_sam_payload_capacity(272U, false), ==, 252U);
    munit_assert_size(seader_sam_payload_capacity(272U, true), ==, 250U);
    munit_assert_size(seader_sam_payload_capacity(21U, false), ==, 1U);
    munit_assert_size(seader_sam_payload_capacity(22U, true), ==, 0U);
    return MUNIT_OK;
}

static MunitTest tests[] = {
    {"/headroom", test_headroom_constants, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/offsets", test_payload_offsets, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/capacity", test_payload_capacity, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
};

MunitSuite test_sam_tx_layout_suite = {
    "/sam-tx-layout",
    tests,
    NULL,
    1,
    MUNIT_SUITE_OPTION_NONE,
};
