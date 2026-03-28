#include "munit.h"
#include "uhf_session_lifecycle.h"

static MunitResult test_expansion_plan_disabled(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    const SeaderUhfExpansionAcquirePlan plan =
        seader_uhf_plan_expansion_acquire(SeaderUhfExpansionStateDisabled);

    munit_assert_false(plan.should_disable);
    munit_assert_false(plan.should_restore);
    return MUNIT_OK;
}

static MunitResult test_expansion_plan_enabled(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    const SeaderUhfExpansionAcquirePlan plan =
        seader_uhf_plan_expansion_acquire(SeaderUhfExpansionStateEnabled);

    munit_assert_true(plan.should_disable);
    munit_assert_true(plan.should_restore);
    return MUNIT_OK;
}

static MunitResult test_expansion_plan_running(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    const SeaderUhfExpansionAcquirePlan plan =
        seader_uhf_plan_expansion_acquire(SeaderUhfExpansionStateRunning);

    munit_assert_true(plan.should_disable);
    munit_assert_true(plan.should_restore);
    return MUNIT_OK;
}

static MunitResult test_expansion_plan_unknown_defaults_to_restore(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    const SeaderUhfExpansionAcquirePlan plan =
        seader_uhf_plan_expansion_acquire(SeaderUhfExpansionStateUnknown);

    munit_assert_true(plan.should_disable);
    munit_assert_true(plan.should_restore);
    return MUNIT_OK;
}

static MunitResult test_restore_expansion_passthrough(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    munit_assert_false(seader_uhf_should_restore_expansion(false));
    munit_assert_true(seader_uhf_should_restore_expansion(true));
    return MUNIT_OK;
}

static MunitResult test_power_acquire_plan_uses_external_vbus_when_present(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    const SeaderUhfPowerAcquirePlan plan = seader_uhf_plan_power_acquire(false, 5200U);

    munit_assert_false(plan.should_enable_otg);
    munit_assert_false(plan.owns_otg);
    munit_assert_true(plan.use_external_vbus);
    return MUNIT_OK;
}

static MunitResult test_power_acquire_plan_requests_otg_when_no_power_exists(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    const SeaderUhfPowerAcquirePlan plan = seader_uhf_plan_power_acquire(false, 0U);

    munit_assert_true(plan.should_enable_otg);
    munit_assert_true(plan.owns_otg);
    munit_assert_false(plan.use_external_vbus);
    return MUNIT_OK;
}

static MunitResult test_power_acquire_plan_does_not_claim_existing_otg(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    const SeaderUhfPowerAcquirePlan plan = seader_uhf_plan_power_acquire(true, 0U);

    munit_assert_false(plan.should_enable_otg);
    munit_assert_false(plan.owns_otg);
    munit_assert_false(plan.use_external_vbus);
    return MUNIT_OK;
}

static MunitResult test_power_available_with_otg_or_external_vbus(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    munit_assert_true(seader_uhf_power_is_available(true, 0U));
    munit_assert_true(seader_uhf_power_is_available(false, 5200U));
    munit_assert_false(seader_uhf_power_is_available(false, 0U));
    return MUNIT_OK;
}

static MunitResult test_power_availability_threshold_boundary(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    munit_assert_false(seader_uhf_power_is_available(false, 4499U));
    munit_assert_true(seader_uhf_power_is_available(false, 4500U));
    return MUNIT_OK;
}

static MunitResult test_power_fault_is_only_fatal_without_external_vbus(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    munit_assert_false(seader_uhf_power_fault_is_fatal(false, 0U));
    munit_assert_false(seader_uhf_power_fault_is_fatal(true, 5200U));
    munit_assert_true(seader_uhf_power_fault_is_fatal(true, 0U));
    return MUNIT_OK;
}

static MunitResult test_disable_owned_otg_only_when_owned_and_enabled(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    munit_assert_false(seader_uhf_should_disable_owned_otg(false, true));
    munit_assert_false(seader_uhf_should_disable_owned_otg(true, false));
    munit_assert_true(seader_uhf_should_disable_owned_otg(true, true));
    return MUNIT_OK;
}

static MunitResult test_disable_owned_otg_when_fault_happens_before_power_enabled(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    munit_assert_true(seader_uhf_should_disable_owned_otg(true, true));
    return MUNIT_OK;
}

static MunitResult test_power_off_after_temporary_probe_failure(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    munit_assert_false(seader_uhf_should_power_off_after_probe(false, false));
    munit_assert_false(seader_uhf_should_power_off_after_probe(true, true));
    munit_assert_true(seader_uhf_should_power_off_after_probe(true, false));
    return MUNIT_OK;
}

static MunitResult test_ack_access_password_only_for_4_byte_payload(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    munit_assert_false(seader_uhf_should_ack_access_password(0U));
    munit_assert_false(seader_uhf_should_ack_access_password(3U));
    munit_assert_true(seader_uhf_should_ack_access_password(4U));
    munit_assert_false(seader_uhf_should_ack_access_password(5U));
    return MUNIT_OK;
}

static MunitTest test_uhf_session_lifecycle_cases[] = {
    {(char*)"/expansion-disabled", test_expansion_plan_disabled, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/expansion-enabled", test_expansion_plan_enabled, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/expansion-running", test_expansion_plan_running, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/expansion-unknown", test_expansion_plan_unknown_defaults_to_restore, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/restore-expansion", test_restore_expansion_passthrough, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/power-acquire-external-vbus", test_power_acquire_plan_uses_external_vbus_when_present, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/power-acquire-enable-otg", test_power_acquire_plan_requests_otg_when_no_power_exists, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/power-acquire-existing-otg", test_power_acquire_plan_does_not_claim_existing_otg, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/power-available", test_power_available_with_otg_or_external_vbus, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/power-threshold", test_power_availability_threshold_boundary, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/power-fault-fatal", test_power_fault_is_only_fatal_without_external_vbus, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/disable-owned-otg", test_disable_owned_otg_only_when_owned_and_enabled, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/disable-owned-otg-fault-before-enabled", test_disable_owned_otg_when_fault_happens_before_power_enabled, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/probe-failure-power-off", test_power_off_after_temporary_probe_failure, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/ack-access-password", test_ack_access_password_only_for_4_byte_payload, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {NULL, NULL, NULL, NULL, 0, NULL},
};

MunitSuite test_uhf_session_lifecycle_suite = {
    "",
    test_uhf_session_lifecycle_cases,
    NULL,
    1,
    MUNIT_SUITE_OPTION_NONE,
};
