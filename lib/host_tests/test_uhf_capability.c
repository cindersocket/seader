#include "munit.h"
#include "uhf_capability.h"

static MunitResult test_root_menu_hidden_without_module_or_sam_support(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    munit_assert_false(
        seader_uhf_should_show_root_menu(false, SeaderUhfSamSupportUnknown));
    munit_assert_false(
        seader_uhf_should_show_root_menu(false, SeaderUhfSamSupportUnavailable));
    munit_assert_false(
        seader_uhf_root_leads_to_no_module(false, SeaderUhfSamSupportUnavailable));
    munit_assert_true(
        seader_uhf_should_show_root_unavailable(false, SeaderUhfSamSupportUnavailable));
    return MUNIT_OK;
}

static MunitResult test_root_menu_shown_for_module_only(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    munit_assert_true(
        seader_uhf_should_show_root_menu(true, SeaderUhfSamSupportUnavailable));
    munit_assert_false(
        seader_uhf_should_show_root_unavailable(true, SeaderUhfSamSupportUnavailable));
    munit_assert_false(
        seader_uhf_root_leads_to_no_module(true, SeaderUhfSamSupportUnavailable));
    return MUNIT_OK;
}

static MunitResult test_root_menu_shown_for_sam_support_only(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    munit_assert_true(
        seader_uhf_should_show_root_menu(false, SeaderUhfSamSupportSupported));
    munit_assert_false(
        seader_uhf_should_show_root_unavailable(false, SeaderUhfSamSupportSupported));
    munit_assert_true(
        seader_uhf_root_leads_to_no_module(false, SeaderUhfSamSupportSupported));
    return MUNIT_OK;
}

static MunitResult test_root_menu_module_and_sam_support_prefers_read_menu(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    munit_assert_true(seader_uhf_should_show_root_menu(true, SeaderUhfSamSupportSupported));
    munit_assert_false(
        seader_uhf_should_show_root_unavailable(true, SeaderUhfSamSupportSupported));
    munit_assert_false(
        seader_uhf_root_leads_to_no_module(true, SeaderUhfSamSupportSupported));
    return MUNIT_OK;
}

static MunitResult test_sio_label_without_sam(const MunitParameter params[], void* fixture) {
    (void)params;
    (void)fixture;

    munit_assert_string_equal(
        seader_uhf_sio_menu_label(false, SeaderUhfSamSupportSupported), "No SAM");
    munit_assert_false(seader_uhf_sio_menu_enabled(false, SeaderUhfSamSupportSupported));
    return MUNIT_OK;
}

static MunitResult test_sio_label_when_support_unknown(const MunitParameter params[], void* fixture) {
    (void)params;
    (void)fixture;

    munit_assert_string_equal(
        seader_uhf_sio_menu_label(true, SeaderUhfSamSupportUnknown),
        "Checking SAM UHF support");
    munit_assert_false(seader_uhf_sio_menu_enabled(true, SeaderUhfSamSupportUnknown));
    return MUNIT_OK;
}

static MunitResult test_sio_label_when_support_missing(const MunitParameter params[], void* fixture) {
    (void)params;
    (void)fixture;

    munit_assert_string_equal(
        seader_uhf_sio_menu_label(true, SeaderUhfSamSupportUnavailable),
        "SAM lacks UHF support");
    munit_assert_false(seader_uhf_sio_menu_enabled(true, SeaderUhfSamSupportUnavailable));
    return MUNIT_OK;
}

static MunitResult test_sio_label_when_supported(const MunitParameter params[], void* fixture) {
    (void)params;
    (void)fixture;

    munit_assert_string_equal(
        seader_uhf_sio_menu_label(true, SeaderUhfSamSupportSupported), "Read SIO");
    munit_assert_true(seader_uhf_sio_menu_enabled(true, SeaderUhfSamSupportSupported));
    return MUNIT_OK;
}

static MunitResult test_raw_result_modes(const MunitParameter params[], void* fixture) {
    (void)params;
    (void)fixture;

    munit_assert_false(seader_uhf_read_mode_is_raw_result(SeaderUhfReadModeNone));
    munit_assert_false(seader_uhf_read_mode_is_raw_result(SeaderUhfReadModeSio));
    munit_assert_true(seader_uhf_read_mode_is_raw_result(SeaderUhfReadModeEpc));
    munit_assert_true(seader_uhf_read_mode_is_raw_result(SeaderUhfReadModeTid));
    return MUNIT_OK;
}

static MunitResult test_sam_support_from_either_key_probe(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    munit_assert_int(
        seader_uhf_sam_support_from_key_probe(true, false), ==, SeaderUhfSamSupportSupported);
    munit_assert_int(
        seader_uhf_sam_support_from_key_probe(false, true), ==, SeaderUhfSamSupportSupported);
    return MUNIT_OK;
}

static MunitResult test_sam_support_from_missing_key_probe(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    munit_assert_int(
        seader_uhf_sam_support_from_key_probe(false, false),
        ==,
        SeaderUhfSamSupportUnavailable);
    return MUNIT_OK;
}

static MunitResult test_probe_error_classifies_protected_6982(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    const uint8_t data[] = {0x69U, 0x82U};
    munit_assert_int(
        seader_uhf_snmp_classify_probe_error(0x06U, data, sizeof(data)),
        ==,
        SeaderUhfSnmpProbeProtected);
    return MUNIT_OK;
}

static MunitResult test_probe_error_classifies_protected_3800(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    const uint8_t data[] = {0x38U, 0x00U};
    munit_assert_int(
        seader_uhf_snmp_classify_probe_error(0x11U, data, sizeof(data)),
        ==,
        SeaderUhfSnmpProbeProtected);
    return MUNIT_OK;
}

static MunitResult test_probe_error_classifies_missing_3900(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    const uint8_t data[] = {0x39U, 0x00U};
    munit_assert_int(
        seader_uhf_snmp_classify_probe_error(0x11U, data, sizeof(data)),
        ==,
        SeaderUhfSnmpProbeMissing);
    return MUNIT_OK;
}

static MunitResult test_probe_error_classifies_missing_2e00_legacy(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    const uint8_t data[] = {0x2EU, 0x00U};
    munit_assert_int(
        seader_uhf_snmp_classify_probe_error(0x11U, data, sizeof(data)),
        ==,
        SeaderUhfSnmpProbeMissing);
    return MUNIT_OK;
}

static MunitResult test_probe_error_classifies_unexpected(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    const uint8_t data[] = {0xFFU, 0xE8U};
    munit_assert_int(
        seader_uhf_snmp_classify_probe_error(0x11U, data, sizeof(data)),
        ==,
        SeaderUhfSnmpProbeUnexpected);
    return MUNIT_OK;
}

static MunitResult test_elite_ice1803_match(const MunitParameter params[], void* fixture) {
    (void)params;
    (void)fixture;

    const uint8_t value[] = {'I', 'C', 'E', '1', '8', '0', '3'};
    munit_assert_true(seader_uhf_is_elite_ice1803(value, sizeof(value)));
    return MUNIT_OK;
}

static MunitResult test_elite_ice1803_mismatch(const MunitParameter params[], void* fixture) {
    (void)params;
    (void)fixture;

    const uint8_t wrong[] = {'I', 'C', 'E', '1', '8', '0', '4'};
    munit_assert_false(seader_uhf_is_elite_ice1803(wrong, sizeof(wrong)));
    munit_assert_false(seader_uhf_is_elite_ice1803(NULL, 0U));
    return MUNIT_OK;
}

static MunitTest test_uhf_capability_cases[] = {
    {(char*)"/root-hidden", test_root_menu_hidden_without_module_or_sam_support, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/root-module-only", test_root_menu_shown_for_module_only, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/root-sam-only", test_root_menu_shown_for_sam_support_only, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/root-module-and-sam", test_root_menu_module_and_sam_support_prefers_read_menu, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/sio-no-sam", test_sio_label_without_sam, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/sio-unknown", test_sio_label_when_support_unknown, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/sio-missing", test_sio_label_when_support_missing, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/sio-supported", test_sio_label_when_supported, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/raw-result-modes", test_raw_result_modes, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/key-probe-supported", test_sam_support_from_either_key_probe, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/key-probe-missing", test_sam_support_from_missing_key_probe, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/probe-protected-6982", test_probe_error_classifies_protected_6982, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/probe-protected-3800", test_probe_error_classifies_protected_3800, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/probe-missing-3900", test_probe_error_classifies_missing_3900, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/probe-missing-2e00-legacy", test_probe_error_classifies_missing_2e00_legacy, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/probe-unexpected", test_probe_error_classifies_unexpected, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/elite-ice1803-match", test_elite_ice1803_match, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/elite-ice1803-mismatch", test_elite_ice1803_mismatch, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {NULL, NULL, NULL, NULL, 0, NULL},
};

MunitSuite test_uhf_capability_suite = {
    "",
    test_uhf_capability_cases,
    NULL,
    1,
    MUNIT_SUITE_OPTION_NONE,
};
