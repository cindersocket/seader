#include <string.h>

#include "munit.h"
#include "uhf_logic.h"

static MunitResult test_build_sam_csn_from_public_tid(const MunitParameter params[], void* fixture) {
    (void)params;
    (void)fixture;

    const uint8_t public_tid[12] =
        {0xE2, 0x80, 0x11, 0x05, 0x00, 0x00, 0x7A, 0x9C, 0xF7, 0x50, 0x08, 0xD8};
    const uint8_t expected[16] = {0xE2, 0x80, 0x11, 0x05, 0x00, 0x00, 0x72, 0x38,
                                  0x00, 0x00, 0x7A, 0x9C, 0xF7, 0x50, 0x08, 0xD8};
    uint8_t actual[SEADER_UHF_NORMALIZED_CSN_LEN] = {0};
    size_t actual_len = 0U;

    munit_assert_true(seader_uhf_build_sam_csn_from_public_tid(
        public_tid, sizeof(public_tid), actual, sizeof(actual), &actual_len));
    munit_assert_size(actual_len, ==, sizeof(expected));
    munit_assert_memory_equal(sizeof(expected), actual, expected);
    return MUNIT_OK;
}

static MunitResult test_build_sam_csn_from_public_epc_form(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    const uint8_t public_epc[12] =
        {0x00, 0x00, 0x72, 0x38, 0x00, 0x00, 0x76, 0x52, 0xF7, 0x4C, 0x08, 0xD8};
    const uint8_t expected[16] = {0xE2, 0x80, 0x11, 0x05, 0x00, 0x00, 0x72, 0x38,
                                  0x00, 0x00, 0x76, 0x52, 0xF7, 0x4C, 0x08, 0xD8};
    uint8_t actual[SEADER_UHF_NORMALIZED_CSN_LEN] = {0};
    size_t actual_len = 0U;

    munit_assert_true(seader_uhf_build_sam_csn_from_form(
        public_epc, sizeof(public_epc), actual, sizeof(actual), &actual_len));
    munit_assert_size(actual_len, ==, sizeof(expected));
    munit_assert_memory_equal(sizeof(expected), actual, expected);
    return MUNIT_OK;
}

static MunitResult test_build_sam_csn_from_private_form(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    const uint8_t private_tid[12] =
        {0xE2, 0x80, 0x11, 0x05, 0x20, 0x00, 0x76, 0x52, 0xF7, 0x4C, 0x08, 0xD8};
    const uint8_t expected[16] = {0xE2, 0x80, 0x11, 0x05, 0x00, 0x00, 0x72, 0x38,
                                  0x00, 0x00, 0x76, 0x52, 0xF7, 0x4C, 0x08, 0xD8};
    uint8_t actual[SEADER_UHF_NORMALIZED_CSN_LEN] = {0};
    size_t actual_len = 0U;

    munit_assert_true(seader_uhf_build_sam_csn_from_form(
        private_tid, sizeof(private_tid), actual, sizeof(actual), &actual_len));
    munit_assert_size(actual_len, ==, sizeof(expected));
    munit_assert_memory_equal(sizeof(expected), actual, expected);
    return MUNIT_OK;
}

static MunitResult test_build_private_tid_from_public_epc_form(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    const uint8_t public_epc[12] =
        {0x00, 0x00, 0x72, 0x38, 0x00, 0x00, 0x7A, 0xD4, 0x91, 0x52, 0x0A, 0xD1};
    const uint8_t expected[12] =
        {0xE2, 0x80, 0x11, 0x05, 0x20, 0x00, 0x7A, 0xD4, 0x91, 0x52, 0x0A, 0xD1};
    uint8_t actual[SEADER_UHF_TRACE_TID_LEN] = {0};
    size_t actual_len = 0U;

    munit_assert_true(seader_uhf_build_private_tid_from_form(
        public_epc, sizeof(public_epc), actual, sizeof(actual), &actual_len));
    munit_assert_size(actual_len, ==, sizeof(expected));
    munit_assert_memory_equal(sizeof(expected), actual, expected);
    return MUNIT_OK;
}

static MunitResult test_build_private_tid_from_public_tid_form(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    const uint8_t public_tid[12] =
        {0xE2, 0x80, 0x11, 0x05, 0x00, 0x00, 0x7A, 0xD4, 0x91, 0x52, 0x0A, 0xD1};
    const uint8_t expected[12] =
        {0xE2, 0x80, 0x11, 0x05, 0x20, 0x00, 0x7A, 0xD4, 0x91, 0x52, 0x0A, 0xD1};
    uint8_t actual[SEADER_UHF_TRACE_TID_LEN] = {0};
    size_t actual_len = 0U;

    munit_assert_true(seader_uhf_build_private_tid_from_form(
        public_tid, sizeof(public_tid), actual, sizeof(actual), &actual_len));
    munit_assert_size(actual_len, ==, sizeof(expected));
    munit_assert_memory_equal(sizeof(expected), actual, expected);
    return MUNIT_OK;
}

static MunitResult test_extract_trace_tid_from_tid_bank(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    const uint8_t tid_bank[24] = {
        0xE2, 0x80, 0x11, 0x05, 0x20, 0x00, 0x7A, 0xD4, 0x91, 0x52, 0x0A, 0xD1,
        0x00, 0x00, 0x72, 0x38, 0x00, 0x00, 0x7A, 0xD4, 0x91, 0x52, 0x0A, 0xD1};
    const uint8_t expected[12] = {
        0xE2, 0x80, 0x11, 0x05, 0x20, 0x00, 0x7A, 0xD4, 0x91, 0x52, 0x0A, 0xD1};
    uint8_t actual[SEADER_UHF_TRACE_TID_LEN] = {0};
    size_t actual_len = 0U;

    munit_assert_true(seader_uhf_extract_trace_tid_from_tid_bank(
        tid_bank, sizeof(tid_bank), actual, sizeof(actual), &actual_len));
    munit_assert_size(actual_len, ==, sizeof(expected));
    munit_assert_memory_equal(sizeof(expected), actual, expected);
    return MUNIT_OK;
}

static MunitResult test_classify_public_tid(const MunitParameter params[], void* fixture) {
    (void)params;
    (void)fixture;

    const uint8_t public_tid[12] =
        {0xE2, 0x80, 0x11, 0x05, 0x00, 0x00, 0x7A, 0x9C, 0xF7, 0x50, 0x08, 0xD8};
    munit_assert_int(
        seader_uhf_classify_tid_view(public_tid, sizeof(public_tid)), ==, SeaderUhfTidViewPublic);
    return MUNIT_OK;
}

static MunitResult test_classify_private_tid(const MunitParameter params[], void* fixture) {
    (void)params;
    (void)fixture;

    const uint8_t private_tid[12] =
        {0xE2, 0x80, 0x11, 0x05, 0x20, 0x00, 0x7A, 0x9C, 0xF7, 0x50, 0x08, 0xD8};
    munit_assert_int(
        seader_uhf_classify_tid_view(private_tid, sizeof(private_tid)), ==, SeaderUhfTidViewPrivate);
    return MUNIT_OK;
}

static MunitResult test_private_read_plan_already_unlocked(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    SeaderUhfPrivateReadPlan plan = seader_uhf_plan_private_read(true, true, 0x0000U);
    munit_assert_true(plan.can_read);
    munit_assert_false(plan.should_unlock);
    munit_assert_true(plan.should_relock);
    munit_assert_int(plan.mode, ==, SeaderUhfPrivateReadModeDirect);
    return MUNIT_OK;
}

static MunitResult test_private_read_plan_public_locked(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    SeaderUhfPrivateReadPlan plan = seader_uhf_plan_private_read(true, true, 0x4000U);
    munit_assert_true(plan.can_read);
    munit_assert_true(plan.should_unlock);
    munit_assert_true(plan.should_relock);
    munit_assert_int(plan.mode, ==, SeaderUhfPrivateReadModeUnlockThenRead);
    return MUNIT_OK;
}

static MunitResult test_private_read_plan_without_password(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    SeaderUhfPrivateReadPlan plan = seader_uhf_plan_private_read(false, true, 0x4000U);
    munit_assert_false(plan.can_read);
    return MUNIT_OK;
}

static MunitResult test_private_read_plan_without_qt_knowledge(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    SeaderUhfPrivateReadPlan plan = seader_uhf_plan_private_read(true, false, 0x4000U);
    munit_assert_true(plan.can_read);
    munit_assert_false(plan.should_unlock);
    munit_assert_true(plan.should_relock);
    munit_assert_int(plan.mode, ==, SeaderUhfPrivateReadModeDirect);
    return MUNIT_OK;
}

static MunitResult test_normalize_private_data_for_sam(const MunitParameter params[], void* fixture) {
    (void)params;
    (void)fixture;

    const uint8_t tid_bank[24] = {
        0xE2, 0x80, 0x11, 0x05, 0x20, 0x00, 0x76, 0x52, 0xF7, 0x4C, 0x08, 0xD8,
        0x00, 0x00, 0x72, 0x38, 0x00, 0x00, 0x76, 0x52, 0xF7, 0x4C, 0x08, 0xD8};
    const uint8_t user_data_in[] = {
        0x30, 0x33, 0x81, 0x06, 0x01, 0x84, 0xDE, 0x38, 0x83, 0x38, 0xA5, 0x02, 0x05, 0x00,
        0xA6, 0x08, 0x81, 0x01, 0x01, 0x04, 0x03, 0x03, 0x00, 0x08, 0xA7, 0x17, 0x85, 0x15,
        0x4C, 0xD3, 0x4C, 0x0C, 0x58, 0xB3, 0xF4, 0xC3, 0x3E, 0xF5, 0xDF, 0xCB, 0x4B, 0x35,
        0xCC, 0x11, 0xA8, 0x3D, 0x00, 0x02, 0xE6, 0xA9, 0x02, 0x05, 0x00, 0x05, 0x00};
    uint8_t tid_out[SEADER_UHF_TRACE_TID_LEN] = {0};
    uint8_t user_data_out[SEADER_UHF_SAM_USER_DATA_LEN] = {0};
    size_t tid_out_len = 0U;
    size_t user_data_out_len = 0U;

    munit_assert_true(seader_uhf_normalize_private_data_for_sam(
        tid_bank,
        sizeof(tid_bank),
        user_data_in,
        sizeof(user_data_in),
        tid_out,
        sizeof(tid_out),
        &tid_out_len,
        user_data_out,
        sizeof(user_data_out),
        &user_data_out_len));
    munit_assert_size(tid_out_len, ==, SEADER_UHF_TRACE_TID_LEN);
    munit_assert_memory_equal(SEADER_UHF_TRACE_TID_LEN, tid_out, tid_bank);
    munit_assert_size(user_data_out_len, ==, SEADER_UHF_SAM_USER_DATA_LEN);
    munit_assert_memory_equal(sizeof(user_data_in), user_data_out, user_data_in);
    for(size_t i = sizeof(user_data_in); i < SEADER_UHF_SAM_USER_DATA_LEN; i++) {
        munit_assert_uint8(user_data_out[i], ==, 0U);
    }
    return MUNIT_OK;
}

static MunitResult test_inventory_commit_requires_full_identification(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    munit_assert_false(seader_uhf_inventory_identification_complete(false, false, false, false));
    munit_assert_false(seader_uhf_inventory_identification_complete(true, false, false, false));
    munit_assert_false(seader_uhf_inventory_identification_complete(true, true, false, false));
    munit_assert_true(seader_uhf_inventory_identification_complete(true, true, false, true));
    munit_assert_true(seader_uhf_inventory_identification_complete(true, true, true, true));
    return MUNIT_OK;
}

static MunitResult test_describe_public_epc_form(const MunitParameter params[], void* fixture) {
    (void)params;
    (void)fixture;

    const uint8_t public_epc[12] =
        {0x00, 0x00, 0x72, 0x38, 0x00, 0x00, 0x76, 0x52, 0xF7, 0x4C, 0x08, 0xD8};
    munit_assert_string_equal(seader_uhf_describe_form(public_epc, sizeof(public_epc)), "public-epc");
    return MUNIT_OK;
}

static MunitResult test_public_epc_helper_matches_live_prefix(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    const uint8_t public_epc[12] =
        {0x00, 0x00, 0x72, 0x38, 0x00, 0x00, 0x7A, 0xD4, 0x91, 0x52, 0x0A, 0xD1};
    const uint8_t private_epc[12] =
        {0x31, 0x31, 0x31, 0x30, 0x32, 0x38, 0x37, 0x39, 0x33, 0x30, 0x37, 0x2D};

    munit_assert_true(seader_uhf_is_public_epc_form(public_epc, sizeof(public_epc)));
    munit_assert_false(seader_uhf_is_public_epc_form(private_epc, sizeof(private_epc)));
    return MUNIT_OK;
}

static MunitResult test_describe_unknown_workorder_form(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    const uint8_t workorder_epc[12] =
        {0x30, 0x31, 0x32, 0x33, 0xAA, 0xBB, 0x76, 0x52, 0xF7, 0x4C, 0x08, 0xD8};
    munit_assert_string_equal(
        seader_uhf_describe_form(workorder_epc, sizeof(workorder_epc)), "workorder-or-unknown");
    return MUNIT_OK;
}

static MunitTest test_uhf_logic_cases[] = {
    {(char*)"/build-sam-csn", test_build_sam_csn_from_public_tid, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/build-sam-csn-public-epc", test_build_sam_csn_from_public_epc_form, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/build-sam-csn-private-form", test_build_sam_csn_from_private_form, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/build-private-tid-public-epc", test_build_private_tid_from_public_epc_form, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/build-private-tid-public-tid", test_build_private_tid_from_public_tid_form, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/extract-trace-tid-from-bank", test_extract_trace_tid_from_tid_bank, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/classify-public", test_classify_public_tid, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/classify-private", test_classify_private_tid, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/plan-unlocked", test_private_read_plan_already_unlocked, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/plan-locked", test_private_read_plan_public_locked, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/plan-no-password", test_private_read_plan_without_password, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/plan-no-qt", test_private_read_plan_without_qt_knowledge, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/normalize-private-data", test_normalize_private_data_for_sam, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/inventory-commit", test_inventory_commit_requires_full_identification, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/describe-public-epc", test_describe_public_epc_form, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/public-epc-helper", test_public_epc_helper_matches_live_prefix, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/describe-workorder", test_describe_unknown_workorder_form, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {NULL, NULL, NULL, NULL, 0, NULL},
};

MunitSuite test_uhf_logic_suite = {
    "",
    test_uhf_logic_cases,
    NULL,
    1,
    MUNIT_SUITE_OPTION_NONE,
};
