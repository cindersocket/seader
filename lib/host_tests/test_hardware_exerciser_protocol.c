#include "munit.h"

#include "hardware_exerciser/protocol.h"

/*
 * These tests lock down the wire-level helpers used by HardwareExerciser so transport framing can
 * evolve safely without requiring firmware runtime coverage.
 */
static MunitResult test_parse_complete_escape_frame(const MunitParameter params[], void* fixture) {
    (void)params;
    (void)fixture;

    /* Complete escape frame should parse, expose its full length, and validate its trailing LRC. */
    uint8_t frame[] = {
        HX_SYNC,
        HX_CTRL,
        HX_CCID_PC_TO_RDR_ESCAPE,
        0x04,
        0x00,
        0x00,
        0x00,
        0x01,
        0x22,
        0x00,
        0x00,
        0x00,
        'H',
        'X',
        HX_PROTOCOL_VERSION,
        HxOpcodePing,
        0x00,
    };
    frame[sizeof(frame) - 1U] = hx_calc_lrc(frame, sizeof(frame) - 1U);

    size_t frame_len = 0U;
    munit_assert_int(
        hx_ccid_frame_state(frame, sizeof(frame), sizeof(frame), &frame_len), ==, HxFrameStateReady);
    munit_assert_size(frame_len, ==, sizeof(frame));
    munit_assert_true(hx_ccid_is_escape_frame(frame, sizeof(frame)));
    munit_assert_true(hx_validate_lrc(frame, sizeof(frame)));
    return MUNIT_OK;
}

static MunitResult test_partial_frame_needs_more(const MunitParameter params[], void* fixture) {
    (void)params;
    (void)fixture;

    /* Short reads must stay resumable instead of being mistaken for framing loss. */
    const uint8_t frame[] = {HX_SYNC, HX_CTRL, HX_CCID_PC_TO_RDR_ESCAPE, 0x04};
    size_t frame_len = 0U;
    munit_assert_int(
        hx_ccid_frame_state(frame, sizeof(frame), 64U, &frame_len), ==, HxFrameStateNeedMore);
    return MUNIT_OK;
}

static MunitResult test_desync_detected(const MunitParameter params[], void* fixture) {
    (void)params;
    (void)fixture;

    /* Bad sync bytes let the caller advance by one byte and search for the next frame boundary. */
    const uint8_t frame[] = {0x00, HX_CTRL, HX_CCID_PC_TO_RDR_ESCAPE};
    size_t frame_len = 0U;
    munit_assert_int(
        hx_ccid_frame_state(frame, sizeof(frame), 64U, &frame_len), ==, HxFrameStateDesync);
    return MUNIT_OK;
}

static MunitResult test_declared_frame_too_large_is_invalid(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    /* Declared length larger than the caller's accumulation buffer is a hard framing error. */
    const uint8_t frame[] = {
        HX_SYNC,
        HX_CTRL,
        HX_CCID_PC_TO_RDR_ESCAPE,
        0x40,
        0x00,
        0x00,
        0x00,
        0x01,
        0x22,
        0x00,
        0x00,
        0x00,
    };
    size_t frame_len = 0U;
    munit_assert_int(
        hx_ccid_frame_state(frame, sizeof(frame), 32U, &frame_len), ==, HxFrameStateInvalidLength);
    munit_assert_size(frame_len, ==, 77U);
    return MUNIT_OK;
}

static MunitResult test_declared_frame_overflow_is_invalid(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    /* Overflowed declared payload lengths must be rejected before total frame math wraps. */
    const uint8_t frame[] = {
        HX_SYNC,
        HX_CTRL,
        HX_CCID_PC_TO_RDR_ESCAPE,
        0xff,
        0xff,
        0xff,
        0xff,
        0x01,
        0x22,
        0x00,
        0x00,
        0x00,
    };
    size_t frame_len = 0U;
    munit_assert_int(
        hx_ccid_frame_state(frame, sizeof(frame), 1024U, &frame_len), ==, HxFrameStateInvalidLength);
    return MUNIT_OK;
}

static MunitResult test_parse_request_payload(const MunitParameter params[], void* fixture) {
    (void)params;
    (void)fixture;

    /* Parsed request body is borrowed from the payload slice rather than copied. */
    const uint8_t payload[] = {'H', 'X', HX_PROTOCOL_VERSION, HxOpcodeGetCaps, 0xaa, 0xbb};
    HxRequest request = {0};

    munit_assert_true(hx_parse_request_payload(payload, sizeof(payload), &request));
    munit_assert_uint8(request.opcode, ==, HxOpcodeGetCaps);
    munit_assert_size(request.body_len, ==, 2U);
    munit_assert_uint8(request.body[0], ==, 0xaa);
    munit_assert_uint8(request.body[1], ==, 0xbb);
    return MUNIT_OK;
}

static MunitResult test_copy_response_body_rejects_oversized(const MunitParameter params[], void* fixture) {
    (void)params;
    (void)fixture;

    /* Response body helpers must fail closed instead of silently truncating host-visible bytes. */
    uint8_t out[8] = {0};
    size_t out_len = 123U;
    const uint8_t body[16] = {0};

    munit_assert_false(hx_copy_response_body(body, sizeof(body), out, sizeof(out), &out_len));
    munit_assert_size(out_len, ==, 0U);
    return MUNIT_OK;
}

static MunitResult test_build_hf14a_scan_payload_with_ats(const MunitParameter params[], void* fixture) {
    (void)params;
    (void)fixture;

    /* Scan payload includes UID, ATQA, SAK, and optional ATS bytes in compact wire order. */
    const uint8_t uid[] = {0x04, 0xa1, 0xb2, 0xc3, 0xd4, 0xe5, 0xf6};
    const uint8_t atqa[] = {0x44, 0x00};
    const uint8_t ats[] = {0x75, 0x77, 0x81, 0x02};
    uint8_t out[32] = {0};

    const size_t len =
        hx_build_hf14a_scan_payload(uid, sizeof(uid), atqa, 0x20, ats, sizeof(ats), out, sizeof(out));

    const uint8_t expected[] = {
        0x07, 0x04, 0xa1, 0xb2, 0xc3, 0xd4, 0xe5, 0xf6, 0x44, 0x00, 0x20, 0x04, 0x75, 0x77, 0x81, 0x02};
    munit_assert_size(len, ==, sizeof(expected));
    munit_assert_memory_equal(sizeof(expected), out, expected);
    return MUNIT_OK;
}

static MunitResult test_build_hf14a_scan_payload_without_ats(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    /* ATS length stays explicit even when the scanned tag does not expose ISO14443-4 data. */
    const uint8_t uid[] = {0xde, 0xad, 0xbe, 0xef};
    const uint8_t atqa[] = {0x04, 0x00};
    uint8_t out[16] = {0};

    const size_t len = hx_build_hf14a_scan_payload(uid, sizeof(uid), atqa, 0x08, NULL, 0U, out, sizeof(out));

    const uint8_t expected[] = {0x04, 0xde, 0xad, 0xbe, 0xef, 0x04, 0x00, 0x08, 0x00};
    munit_assert_size(len, ==, sizeof(expected));
    munit_assert_memory_equal(sizeof(expected), out, expected);
    return MUNIT_OK;
}

static MunitResult test_build_hf14a_scan_payload_rejects_small_buffer(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    /* The builder must reject undersized caller buffers rather than partially encoding payloads. */
    const uint8_t uid[] = {0x01, 0x02, 0x03, 0x04};
    const uint8_t atqa[] = {0x44, 0x03};
    uint8_t out[8] = {0};

    munit_assert_size(
        hx_build_hf14a_scan_payload(uid, sizeof(uid), atqa, 0x20, NULL, 0U, out, sizeof(out)), ==, 0U);
    return MUNIT_OK;
}

static MunitResult test_build_escape_response_preserves_slot_and_seq(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    /* Response envelopes must mirror the request slot and sequence for host-side correlation. */
    uint8_t request[] = {
        HX_SYNC,
        HX_CTRL,
        HX_CCID_PC_TO_RDR_ESCAPE,
        0x04,
        0x00,
        0x00,
        0x00,
        0x02,
        0x77,
        0x00,
        0x00,
        0x00,
        'H',
        'X',
        HX_PROTOCOL_VERSION,
        HxOpcodePing,
        0x00,
    };
    request[sizeof(request) - 1U] = hx_calc_lrc(request, sizeof(request) - 1U);

    uint8_t payload[16] = {0};
    const size_t payload_len =
        hx_build_response_payload(HxOpcodePing, HxStatusOk, (const uint8_t*)"OK", 2U, payload, sizeof(payload));
    uint8_t response[64] = {0};
    const size_t response_len = hx_build_ccid_escape_response(
        request, sizeof(request), payload, payload_len, response, sizeof(response));

    munit_assert_size(response_len, >, 0U);
    munit_assert_uint8(response[2], ==, HX_CCID_RDR_TO_PC_ESCAPE);
    munit_assert_uint8(response[7], ==, 0x02);
    munit_assert_uint8(response[8], ==, 0x77);
    munit_assert_true(hx_validate_lrc(response, response_len));
    return MUNIT_OK;
}

static MunitResult test_parse_gpio_configure_request(const MunitParameter params[], void* fixture) {
    (void)params;
    (void)fixture;

    /* GPIO configure requests are fixed-width so malformed bodies fail early. */
    const uint8_t body[] = {0x07, HxGpioModeOutputPushPull, HxGpioPullUp, 0x01};
    HxGpioConfigureRequest request = {0};

    munit_assert_true(hx_parse_gpio_configure_request(body, sizeof(body), &request));
    munit_assert_uint8(request.pin_number, ==, 0x07);
    munit_assert_uint8(request.mode, ==, HxGpioModeOutputPushPull);
    munit_assert_uint8(request.pull, ==, HxGpioPullUp);
    munit_assert_uint8(request.initial_level, ==, 0x01);
    return MUNIT_OK;
}

static MunitResult test_parse_gpio_vector_request(const MunitParameter params[], void* fixture) {
    (void)params;
    (void)fixture;

    /* Vector bodies carry grouped writes, one settle delay, then ordered sample descriptors. */
    const uint8_t body[] = {
        0x02,
        0x07,
        0x01,
        0x15,
        0x00,
        0x2c,
        0x01,
        0x00,
        0x00,
        0x03,
        0x07,
        HxGpioSampleDigital,
        0x15,
        HxGpioSampleAnalog,
        0x16,
        HxGpioSampleDigital,
    };
    HxGpioVectorRequest request = {0};
    HxGpioWriteRequest write = {0};
    HxGpioVectorSample sample = {0};

    munit_assert_true(hx_parse_gpio_vector_request(body, sizeof(body), &request));
    munit_assert_size(request.write_count, ==, 2U);
    munit_assert_uint32(request.settle_us, ==, 300U);
    munit_assert_size(request.sample_count, ==, 3U);

    munit_assert_true(hx_gpio_vector_write_at(&request, 0U, &write));
    munit_assert_uint8(write.pin_number, ==, 0x07);
    munit_assert_uint8(write.level, ==, 0x01);
    munit_assert_true(hx_gpio_vector_write_at(&request, 1U, &write));
    munit_assert_uint8(write.pin_number, ==, 0x15);
    munit_assert_uint8(write.level, ==, 0x00);

    munit_assert_true(hx_gpio_vector_sample_at(&request, 1U, &sample));
    munit_assert_uint8(sample.pin_number, ==, 0x15);
    munit_assert_uint8(sample.sample_kind, ==, HxGpioSampleAnalog);
    return MUNIT_OK;
}

static MunitResult test_parse_gpio_vector_request_rejects_truncated(
    const MunitParameter params[],
    void* fixture) {
    (void)params;
    (void)fixture;

    /* Sample descriptors must consume the remaining body exactly. */
    const uint8_t body[] = {0x01, 0x07, 0x01, 0x10, 0x00, 0x00, 0x00, 0x02, 0x07, 0x00, 0x15};
    HxGpioVectorRequest request = {0};

    munit_assert_false(hx_parse_gpio_vector_request(body, sizeof(body), &request));
    return MUNIT_OK;
}

static MunitResult test_build_gpio_pin_info(const MunitParameter params[], void* fixture) {
    (void)params;
    (void)fixture;

    /* Discovery entries expose connector number, capability flags, and firmware pin name. */
    uint8_t out[16] = {0};
    const size_t len = hx_build_gpio_pin_info(
        0x07, HX_GPIO_PIN_FLAG_DIGITAL | HX_GPIO_PIN_FLAG_ANALOG, "PC3", out, sizeof(out));

    const uint8_t expected[] = {0x07, 0x03, 0x03, 'P', 'C', '3'};
    munit_assert_size(len, ==, sizeof(expected));
    munit_assert_memory_equal(sizeof(expected), out, expected);
    return MUNIT_OK;
}

static MunitResult test_build_gpio_analog_value(const MunitParameter params[], void* fixture) {
    (void)params;
    (void)fixture;

    /* Analog reads return both the raw ADC sample and its millivolt conversion. */
    uint8_t out[8] = {0};
    const size_t len = hx_build_gpio_analog_value(0x0123, 0x0456, out, sizeof(out));

    const uint8_t expected[] = {0x23, 0x01, 0x56, 0x04};
    munit_assert_size(len, ==, sizeof(expected));
    munit_assert_memory_equal(sizeof(expected), out, expected);
    return MUNIT_OK;
}

static MunitResult test_build_gpio_vector_sample(const MunitParameter params[], void* fixture) {
    (void)params;
    (void)fixture;

    /* Vector samples preserve request order and include an aux field for analog millivolts. */
    uint8_t out[8] = {0};
    const size_t len =
        hx_build_gpio_vector_sample(0x15, HxGpioSampleAnalog, 0x0123, 0x0456, out, sizeof(out));

    const uint8_t expected[] = {0x15, HxGpioSampleAnalog, 0x23, 0x01, 0x56, 0x04};
    munit_assert_size(len, ==, sizeof(expected));
    munit_assert_memory_equal(sizeof(expected), out, expected);
    return MUNIT_OK;
}

static MunitTest test_hardware_exerciser_protocol_cases[] = {
    {(char*)"/parse-complete-escape-frame",
     test_parse_complete_escape_frame,
     NULL,
     NULL,
     MUNIT_TEST_OPTION_NONE,
     NULL},
    {(char*)"/partial-frame-needs-more",
     test_partial_frame_needs_more,
     NULL,
     NULL,
     MUNIT_TEST_OPTION_NONE,
     NULL},
    {(char*)"/desync-detected", test_desync_detected, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/declared-frame-too-large-is-invalid",
     test_declared_frame_too_large_is_invalid,
     NULL,
     NULL,
     MUNIT_TEST_OPTION_NONE,
     NULL},
    {(char*)"/declared-frame-overflow-is-invalid",
     test_declared_frame_overflow_is_invalid,
     NULL,
     NULL,
     MUNIT_TEST_OPTION_NONE,
     NULL},
    {(char*)"/parse-request-payload",
     test_parse_request_payload,
     NULL,
     NULL,
     MUNIT_TEST_OPTION_NONE,
     NULL},
    {(char*)"/copy-response-body-rejects-oversized",
     test_copy_response_body_rejects_oversized,
     NULL,
     NULL,
     MUNIT_TEST_OPTION_NONE,
     NULL},
    {(char*)"/build-hf14a-scan-payload-with-ats",
     test_build_hf14a_scan_payload_with_ats,
     NULL,
     NULL,
     MUNIT_TEST_OPTION_NONE,
     NULL},
    {(char*)"/build-hf14a-scan-payload-without-ats",
     test_build_hf14a_scan_payload_without_ats,
     NULL,
     NULL,
     MUNIT_TEST_OPTION_NONE,
     NULL},
    {(char*)"/build-hf14a-scan-payload-rejects-small-buffer",
     test_build_hf14a_scan_payload_rejects_small_buffer,
     NULL,
     NULL,
     MUNIT_TEST_OPTION_NONE,
     NULL},
    {(char*)"/build-escape-response-preserves-slot-and-seq",
     test_build_escape_response_preserves_slot_and_seq,
     NULL,
     NULL,
     MUNIT_TEST_OPTION_NONE,
     NULL},
    {(char*)"/parse-gpio-configure-request",
     test_parse_gpio_configure_request,
     NULL,
     NULL,
     MUNIT_TEST_OPTION_NONE,
     NULL},
    {(char*)"/parse-gpio-vector-request",
     test_parse_gpio_vector_request,
     NULL,
     NULL,
     MUNIT_TEST_OPTION_NONE,
     NULL},
    {(char*)"/parse-gpio-vector-request-rejects-truncated",
     test_parse_gpio_vector_request_rejects_truncated,
     NULL,
     NULL,
     MUNIT_TEST_OPTION_NONE,
     NULL},
    {(char*)"/build-gpio-pin-info",
     test_build_gpio_pin_info,
     NULL,
     NULL,
     MUNIT_TEST_OPTION_NONE,
     NULL},
    {(char*)"/build-gpio-analog-value",
     test_build_gpio_analog_value,
     NULL,
     NULL,
     MUNIT_TEST_OPTION_NONE,
     NULL},
    {(char*)"/build-gpio-vector-sample",
     test_build_gpio_vector_sample,
     NULL,
     NULL,
     MUNIT_TEST_OPTION_NONE,
     NULL},
    {NULL, NULL, NULL, NULL, 0, NULL},
};

MunitSuite test_hardware_exerciser_protocol_suite = {
    "",
    test_hardware_exerciser_protocol_cases,
    NULL,
    1,
    MUNIT_SUITE_OPTION_NONE,
};
