#include "munit.h"
#include "uhf_worker_start.h"

static MunitResult test_worker_start_requires_successful_acquire(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    munit_assert_false(seader_uhf_worker_start_can_continue(false, true, true));
    return MUNIT_OK;
}

static MunitResult test_worker_start_requires_plugin_pointer(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    munit_assert_false(seader_uhf_worker_start_can_continue(true, false, true));
    return MUNIT_OK;
}

static MunitResult test_worker_start_requires_plugin_context(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    munit_assert_false(seader_uhf_worker_start_can_continue(true, true, false));
    return MUNIT_OK;
}

static MunitResult test_worker_start_continues_when_ready(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    munit_assert_true(seader_uhf_worker_start_can_continue(true, true, true));
    return MUNIT_OK;
}

static MunitTest test_uhf_worker_start_cases[] = {
    {(char*)"/acquire-failure", test_worker_start_requires_successful_acquire, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/plugin-missing", test_worker_start_requires_plugin_pointer, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/context-missing", test_worker_start_requires_plugin_context, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/ready", test_worker_start_continues_when_ready, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {NULL, NULL, NULL, NULL, 0, NULL},
};

MunitSuite test_uhf_worker_start_suite = {
    "",
    test_uhf_worker_start_cases,
    NULL,
    1,
    MUNIT_SUITE_OPTION_NONE,
};
