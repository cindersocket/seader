#include <string.h>

#include "munit.h"
#include "uhf_read_lifecycle.h"

static MunitResult test_failure_reason_texts_are_stable(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    munit_assert_string_equal(
        seader_uhf_read_failure_reason_text(PluginUhfReadFailureReasonModuleUnavailable),
        "UHF unavailable");
    munit_assert_string_equal(
        seader_uhf_read_failure_reason_text(PluginUhfReadFailureReasonSelectFailed),
        "UHF tag select failed");
    munit_assert_string_equal(
        seader_uhf_read_failure_reason_text(PluginUhfReadFailureReasonPublicTidReadFailed),
        "UHF tag detected, identity unreadable");
    munit_assert_string_equal(
        seader_uhf_read_failure_reason_text(PluginUhfReadFailureReasonUnlockFailed),
        "UHF unlock failed");
    munit_assert_string_equal(
        seader_uhf_read_failure_reason_text(PluginUhfReadFailureReasonPrivateDataReadFailed),
        "UHF private data read failed");
    munit_assert_string_equal(
        seader_uhf_read_failure_reason_text(PluginUhfReadFailureReasonRelockFailed),
        "UHF relock failed");
    return MUNIT_OK;
}

static MunitResult test_failure_reason_texts_fit_storage(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    munit_assert_size(
        strlen(seader_uhf_read_failure_reason_text(PluginUhfReadFailureReasonPublicTidReadFailed)),
        <,
        96U);
    return MUNIT_OK;
}

static MunitTest test_uhf_read_lifecycle_cases[] = {
    {(char*)"/failure-text", test_failure_reason_texts_are_stable, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/failure-text-fits", test_failure_reason_texts_fit_storage, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {NULL, NULL, NULL, NULL, 0, NULL},
};

MunitSuite test_uhf_read_lifecycle_suite = {
    "",
    test_uhf_read_lifecycle_cases,
    NULL,
    1,
    MUNIT_SUITE_OPTION_NONE,
};
