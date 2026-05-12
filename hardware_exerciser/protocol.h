#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Wire preamble used by the lightweight serial framing that wraps CCID messages. */
#define HX_SYNC 0x03U
#define HX_CTRL 0x06U
#define HX_NAK  0x15U

/* CCID message types used by the host request and device response path. */
#define HX_CCID_PC_TO_RDR_ESCAPE 0x6bU
#define HX_CCID_RDR_TO_PC_ESCAPE 0x83U

/* Request/response payload prefix that identifies the embedded HX protocol. */
#define HX_PROTOCOL_VERSION 1U
#define HX_REQUEST_MAGIC_0  'H'
#define HX_REQUEST_MAGIC_1  'X'

/* Capability bits returned by GetCaps as a little-endian 32-bit mask. */
#define HX_CAP_PING            (1UL << 0)
#define HX_CAP_GET_CAPS        (1UL << 1)
#define HX_CAP_GET_STATUS      (1UL << 2)
#define HX_CAP_HF14A_SCAN      (1UL << 3)
#define HX_CAP_HF14A_TXRX      (1UL << 4)
#define HX_CAP_HF15693_SCAN    (1UL << 5)
#define HX_CAP_HF15693_TXRX    (1UL << 6)
#define HX_CAP_LF_READ_DECODED (1UL << 7)
#define HX_CAP_GPIO_LIST_PINS  (1UL << 8)
#define HX_CAP_GPIO_CONFIGURE  (1UL << 9)
#define HX_CAP_GPIO_WRITE      (1UL << 10)
#define HX_CAP_GPIO_READ       (1UL << 11)
#define HX_CAP_GPIO_READ_ANALOG (1UL << 12)
#define HX_CAP_GPIO_VECTOR     (1UL << 13)
#define HX_CAP_GPIO_RESET      (1UL << 14)
#define HX_CAP_QUIT            (1UL << 15)
#define HX_CAP_BOARD_SNAPSHOT  (1UL << 16)
#define HX_CAP_UHF_POWER       (1UL << 17)
#define HX_CAP_UHF_PROBE       (1UL << 18)
#define HX_CAP_UHF_BRIDGE      (1UL << 19)
#define HX_CAP_UHF_NO5V_TEST   (1UL << 20)

/* Opcodes carried in byte 3 of the HX payload header. */
typedef enum {
    HxOpcodePing = 0x01,
    HxOpcodeGetCaps = 0x02,
    HxOpcodeGetStatus = 0x03,
    HxOpcodeGetBoardSnapshot = 0x04,
    HxOpcodeHf14aScan = 0x10,
    HxOpcodeHf14aTxRx = 0x11,
    HxOpcodeHf15693Scan = 0x20,
    HxOpcodeHf15693TxRx = 0x21,
    HxOpcodeLfReadDecoded = 0x30,
    HxOpcodeGpioListPins = 0x40,
    HxOpcodeGpioConfigure = 0x41,
    HxOpcodeGpioWrite = 0x42,
    HxOpcodeGpioRead = 0x43,
    HxOpcodeGpioReadAnalog = 0x44,
    HxOpcodeGpioVectorCapture = 0x45,
    HxOpcodeGpioReset = 0x46,
    HxOpcodeUhfPower = 0x50,
    HxOpcodeUhfProbe = 0x51,
    HxOpcodeUhfBridgeControl = 0x52,
    HxOpcodeUhfNo5vTest = 0x53,
    HxOpcodeQuit = 0x7e,
    HxOpcodeAbort = 0x7f,
} HxOpcode;

/* Status byte returned in every HX response payload. */
typedef enum {
    HxStatusOk = 0x00,
    HxStatusUnsupported = 0x01,
    HxStatusInvalidRequest = 0x02,
    HxStatusBusy = 0x03,
    HxStatusIoError = 0x04,
    HxStatusTimeout = 0x05,
    HxStatusNotFound = 0x06,
} HxStatus;

typedef enum {
    HxBoardClassUnknown = 0,
    HxBoardClassNone = 1,
    HxBoardClassSamOnly = 2,
    HxBoardClassUhfCarrier = 3,
} HxBoardClass;

typedef enum {
    HxUhfModulePresenceUnknown = 0,
    HxUhfModulePresenceAbsent = 1,
    HxUhfModulePresencePresent = 2,
} HxUhfModulePresence;

typedef enum {
    HxUhfModuleFamilyUnknown = 0,
    HxUhfModuleFamilyM100 = 1,
    HxUhfModuleFamilyQM100 = 2,
} HxUhfModuleFamily;

typedef enum {
    HxUhfHardwareClassUnknown = 0,
    HxUhfHardwareClass20dBm = 1,
    HxUhfHardwareClass26dBm = 2,
    HxUhfHardwareClass30dBm = 3,
} HxUhfHardwareClass;

typedef enum {
    HxUhfPowerActionOff = 0,
    HxUhfPowerActionOn = 1,
    HxUhfPowerActionHibernate = 2,
} HxUhfPowerAction;

typedef enum {
    HxUhfBridgeActionDisable = 0,
    HxUhfBridgeActionEnableIfPresent = 1,
    HxUhfBridgeActionForceEnable = 2,
} HxUhfBridgeAction;

/* Parsed view of an HX payload after the magic/version header has been validated. */
typedef struct {
    uint8_t opcode;
    const uint8_t* body;
    size_t body_len;
} HxRequest;

/* Streaming parse result for a CCID frame carried on the USB CDC bridge. */
typedef enum {
    HxFrameStateNeedMore = 0,
    HxFrameStateReady,
    HxFrameStateDesync,
    HxFrameStateInvalidLength,
} HxFrameState;

typedef enum {
    HxGpioModeInput = 0,
    HxGpioModeOutputPushPull = 1,
    HxGpioModeOutputOpenDrain = 2,
    HxGpioModeAnalog = 3,
} HxGpioMode;

typedef enum {
    HxGpioPullNone = 0,
    HxGpioPullUp = 1,
    HxGpioPullDown = 2,
} HxGpioPull;

typedef enum {
    HxGpioSampleDigital = 0,
    HxGpioSampleAnalog = 1,
} HxGpioSampleKind;

#define HX_GPIO_PIN_FLAG_DIGITAL (1U << 0)
#define HX_GPIO_PIN_FLAG_ANALOG  (1U << 1)

typedef struct {
    uint8_t pin_number;
    uint8_t mode;
    uint8_t pull;
    uint8_t initial_level;
} HxGpioConfigureRequest;

typedef struct {
    uint8_t pin_number;
    uint8_t level;
} HxGpioWriteRequest;

typedef struct {
    uint8_t pin_number;
    uint8_t sample_kind;
} HxGpioVectorSample;

typedef struct {
    const uint8_t* writes;
    size_t write_count;
    uint32_t settle_us;
    const uint8_t* samples;
    size_t sample_count;
} HxGpioVectorRequest;

typedef struct {
    uint8_t action;
} HxUhfPowerRequest;

typedef struct {
    uint8_t action;
} HxUhfBridgeControlRequest;

typedef struct {
    uint8_t family;
    uint8_t hardware_class;
} HxUhfProfile;

/* XOR the provided bytes to produce the frame LRC byte. */
uint8_t hx_calc_lrc(const uint8_t* data, size_t len);

/* Validate that the final byte of a frame matches the computed LRC of the preceding bytes. */
bool hx_validate_lrc(const uint8_t* data, size_t len);

/* Append an LRC byte at data[len] and return the new total length. */
size_t hx_add_lrc(uint8_t* data, size_t len);

/*
 * Inspect a possibly partial CCID frame and report whether more data is needed, a full frame is
 * ready, or the buffer is desynchronized/invalid. When provided, frame_len is populated with the
 * full frame length implied by the header.
 */
HxFrameState hx_ccid_frame_state(
    const uint8_t* data,
    size_t len,
    size_t max_frame_len,
    size_t* frame_len);

/* Return true only for complete, well-formed PC_TO_RDR_ESCAPE CCID frames. */
bool hx_ccid_is_escape_frame(const uint8_t* frame, size_t frame_len);

/*
 * Parse the HX payload embedded inside a CCID escape frame. On success, request->body points into
 * the caller-owned payload buffer and remains valid only as long as that buffer does.
 */
bool hx_parse_request_payload(const uint8_t* payload, size_t payload_len, HxRequest* request);

/* Copy a raw response body into the caller buffer, rejecting NULL pointers or truncation. */
bool hx_copy_response_body(
    const uint8_t* body,
    size_t body_len,
    uint8_t* out,
    size_t out_cap,
    size_t* out_len);

bool hx_parse_gpio_configure_request(
    const uint8_t* body,
    size_t body_len,
    HxGpioConfigureRequest* request);
bool hx_parse_gpio_write_request(
    const uint8_t* body,
    size_t body_len,
    HxGpioWriteRequest* request);
bool hx_parse_gpio_pin_request(const uint8_t* body, size_t body_len, uint8_t* pin_number);
bool hx_parse_gpio_vector_request(
    const uint8_t* body,
    size_t body_len,
    HxGpioVectorRequest* request);
bool hx_gpio_vector_write_at(
    const HxGpioVectorRequest* request,
    size_t index,
    HxGpioWriteRequest* write);
bool hx_gpio_vector_sample_at(
    const HxGpioVectorRequest* request,
    size_t index,
    HxGpioVectorSample* sample);
bool hx_parse_uhf_power_request(
    const uint8_t* body,
    size_t body_len,
    HxUhfPowerRequest* request);
bool hx_parse_uhf_bridge_control_request(
    const uint8_t* body,
    size_t body_len,
    HxUhfBridgeControlRequest* request);
uint8_t hx_board_classify(bool pa4_high, bool pc1_high, bool pc0_high);
uint8_t hx_uhf_frame_checksum(const uint8_t* data, size_t len);
bool hx_uhf_build_frame(
    uint8_t command,
    const uint8_t* payload,
    size_t payload_len,
    uint8_t* frame_out,
    size_t frame_out_cap,
    size_t* frame_out_len);
bool hx_uhf_try_parse_frame(
    const uint8_t* stream,
    size_t stream_len,
    uint8_t expected_type,
    uint8_t expected_cmd,
    uint8_t* payload,
    size_t payload_cap,
    size_t* payload_len);
HxUhfProfile hx_uhf_profile_from_versions(const char* hw_version, const char* sw_version);

/*
 * Encode the HF14A scan response body as:
 *   uid_len | uid | atqa_lo | atqa_hi | sak | ats_len | ats
 */
size_t hx_build_hf14a_scan_payload(
    const uint8_t* uid,
    size_t uid_len,
    const uint8_t atqa[2],
    uint8_t sak,
    const uint8_t* ats,
    size_t ats_len,
    uint8_t* out,
    size_t out_cap);

/*
 * Build an HX response payload as:
 *   'H' | 'X' | version | opcode | status | body...
 */
size_t hx_build_response_payload(
    uint8_t opcode,
    uint8_t status,
    const uint8_t* body,
    size_t body_len,
    uint8_t* out,
    size_t out_cap);
size_t hx_build_gpio_pin_info(
    uint8_t pin_number,
    uint8_t flags,
    const char* name,
    uint8_t* out,
    size_t out_cap);
size_t hx_build_gpio_analog_value(
    uint16_t raw_value,
    uint16_t millivolts,
    uint8_t* out,
    size_t out_cap);
size_t hx_build_gpio_vector_sample(
    uint8_t pin_number,
    uint8_t sample_kind,
    uint16_t value,
    uint16_t aux,
    uint8_t* out,
    size_t out_cap);
size_t hx_build_board_snapshot_payload(
    uint8_t board_class,
    uint8_t pa4_high,
    uint8_t pc1_high,
    uint8_t pc0_high,
    uint8_t pc3_level,
    uint8_t otg_enabled,
    uint8_t otg_fault,
    uint16_t vbus_mv,
    uint8_t* out,
    size_t out_cap);
size_t hx_build_uhf_probe_payload(
    uint8_t presence,
    uint8_t family,
    uint8_t hardware_class,
    const char* hw_version,
    const char* sw_version,
    uint8_t* out,
    size_t out_cap);
size_t hx_build_uhf_power_payload(
    uint8_t power_enabled,
    uint8_t otg_enabled,
    uint8_t otg_fault,
    uint16_t vbus_mv,
    uint8_t* out,
    size_t out_cap);
size_t hx_build_uhf_bridge_payload(
    uint8_t bridge_enabled,
    uint8_t forced,
    uint8_t module_present,
    uint8_t* out,
    size_t out_cap);

/*
 * Wrap an HX response payload in a CCID RDR_TO_PC_ESCAPE frame. The slot and sequence fields are
 * copied from the request so the host can correlate responses without extra state.
 */
size_t hx_build_ccid_escape_response(
    const uint8_t* request_frame,
    size_t request_frame_len,
    const uint8_t* payload,
    size_t payload_len,
    uint8_t* out,
    size_t out_cap);
