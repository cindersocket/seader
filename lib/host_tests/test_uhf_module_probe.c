#include <string.h>

#include "munit.h"
#include "uhf_module_probe.h"

static MunitResult test_parse_version_frame_with_noise(const MunitParameter params[], void* fixture) {
    (void)params;
    (void)fixture;

    const uint8_t frame[] = {
        0x00,
        0xff,
        0xbb,
        0x01,
        0x03,
        0x00,
        0x03,
        'A',
        'B',
        'C',
        0xcd,
        0x7e,
    };
    uint8_t payload[8] = {0};
    size_t payload_len = 0U;

    munit_assert_true(seader_uhf_module_try_parse_frame(
        frame, sizeof(frame), 0x01U, 0x03U, payload, sizeof(payload), &payload_len));
    munit_assert_size(payload_len, ==, 3U);
    munit_assert_memory_equal(3U, payload, ((uint8_t[]){'A', 'B', 'C'}));
    return MUNIT_OK;
}

static MunitResult test_rejects_bad_checksum_and_wrong_command(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    uint8_t bad_checksum[] = {0xbb, 0x01, 0x03, 0x00, 0x01, 'A', 0x00, 0x7e};
    uint8_t wrong_command[] = {0xbb, 0x01, 0x04, 0x00, 0x01, 'A', 0x47, 0x7e};
    uint8_t payload[8] = {0};
    size_t payload_len = 99U;

    munit_assert_false(seader_uhf_module_try_parse_frame(
        bad_checksum,
        sizeof(bad_checksum),
        0x01U,
        0x03U,
        payload,
        sizeof(payload),
        &payload_len));
    munit_assert_size(payload_len, ==, 0U);

    munit_assert_false(seader_uhf_module_try_parse_frame(
        wrong_command,
        sizeof(wrong_command),
        0x01U,
        0x03U,
        payload,
        sizeof(payload),
        &payload_len));
    return MUNIT_OK;
}

static MunitResult test_skips_stale_incomplete_candidate(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    const uint8_t stream[] = {
        0xbb,
        0x99,
        0x99,
        0xff,
        0xff,
        0x00,
        0xbb,
        0x01,
        0x03,
        0x00,
        0x01,
        'M',
        0x52,
        0x7e,
    };
    uint8_t payload[4] = {0};
    size_t payload_len = 0U;

    munit_assert_true(seader_uhf_module_try_parse_frame(
        stream, sizeof(stream), 0x01U, 0x03U, payload, sizeof(payload), &payload_len));
    munit_assert_size(payload_len, ==, 1U);
    munit_assert_uint8(payload[0], ==, 'M');
    return MUNIT_OK;
}

static MunitResult test_classifies_m100_26dbm_versions(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    SeaderUhfModuleInfo info;
    const uint8_t hw[] = "M100 26dBm V1.0";
    const uint8_t sw[] = "V2.3.5";

    seader_uhf_module_info_set_versions(&info, hw, strlen((const char*)hw), sw, strlen((const char*)sw));
    munit_assert_int(info.family, ==, SeaderUhfModuleFamilyM100);
    munit_assert_int(info.hardware_class, ==, SeaderUhfModuleHardwareM100_26dBm);
    munit_assert_string_equal(info.hw_version, "M100 26dBm V1.0");
    munit_assert_string_equal(info.sw_version, "V2.3.5");
    return MUNIT_OK;
}

static MunitResult test_module_status_label_visibility(const MunitParameter params[], void* fixture) {
    (void)params;
    (void)fixture;

    char label[SEADER_UHF_MODULE_LABEL_MAX_LEN] = {0};
    SeaderUhfModuleInfo info;
    const uint8_t hw[] = "M100 26dBm V1.0";

    seader_uhf_module_status_label_format(
        SeaderBoardAttachmentSamOnly,
        SeaderUhfModuleStatusDetecting,
        NULL,
        label,
        sizeof(label));
    munit_assert_string_equal(label, "");

    seader_uhf_module_status_label_format(
        SeaderBoardAttachmentUhfCarrier,
        SeaderUhfModuleStatusDetecting,
        NULL,
        label,
        sizeof(label));
    munit_assert_string_equal(label, "UHF: detecting...");

    seader_uhf_module_info_set_versions(&info, hw, strlen((const char*)hw), NULL, 0U);
    seader_uhf_module_status_label_format(
        SeaderBoardAttachmentUhfCarrier,
        SeaderUhfModuleStatusPresent,
        &info,
        label,
        sizeof(label));
    munit_assert_string_equal(label, "UHF: M100 26dBm");

    seader_uhf_module_status_label_format(
        SeaderBoardAttachmentUhfCarrier,
        SeaderUhfModuleStatusMissing,
        NULL,
        label,
        sizeof(label));
    munit_assert_string_equal(label, "UHF: No Module");
    return MUNIT_OK;
}

static MunitTest test_uhf_module_probe_cases[] = {
    {(char*)"/parse-version-frame-with-noise",
     test_parse_version_frame_with_noise,
     NULL,
     NULL,
     MUNIT_TEST_OPTION_NONE,
     NULL},
    {(char*)"/reject-bad-frames",
     test_rejects_bad_checksum_and_wrong_command,
     NULL,
     NULL,
     MUNIT_TEST_OPTION_NONE,
     NULL},
    {(char*)"/skip-stale-incomplete-candidate",
     test_skips_stale_incomplete_candidate,
     NULL,
     NULL,
     MUNIT_TEST_OPTION_NONE,
     NULL},
    {(char*)"/classify-m100-26dbm",
     test_classifies_m100_26dbm_versions,
     NULL,
     NULL,
     MUNIT_TEST_OPTION_NONE,
     NULL},
    {(char*)"/module-status-label",
     test_module_status_label_visibility,
     NULL,
     NULL,
     MUNIT_TEST_OPTION_NONE,
     NULL},
    {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
};

MunitSuite test_uhf_module_probe_suite = {
    "/uhf-module-probe",
    test_uhf_module_probe_cases,
    NULL,
    1,
    MUNIT_SUITE_OPTION_NONE,
};
