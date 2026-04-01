#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HX_SYNC 0x03U
#define HX_CTRL 0x06U
#define HX_NAK  0x15U

#define HX_CCID_PC_TO_RDR_ESCAPE 0x6bU
#define HX_CCID_RDR_TO_PC_ESCAPE 0x83U

#define HX_PROTOCOL_VERSION 1U
#define HX_REQUEST_MAGIC_0  'H'
#define HX_REQUEST_MAGIC_1  'X'

#define HX_CAP_PING            (1UL << 0)
#define HX_CAP_GET_CAPS        (1UL << 1)
#define HX_CAP_GET_STATUS      (1UL << 2)
#define HX_CAP_HF14A_SCAN      (1UL << 3)
#define HX_CAP_HF14A_TXRX      (1UL << 4)
#define HX_CAP_HF15693_SCAN    (1UL << 5)
#define HX_CAP_HF15693_TXRX    (1UL << 6)
#define HX_CAP_LF_READ_DECODED (1UL << 7)

typedef enum {
    HxOpcodePing = 0x01,
    HxOpcodeGetCaps = 0x02,
    HxOpcodeGetStatus = 0x03,
    HxOpcodeHf14aScan = 0x10,
    HxOpcodeHf14aTxRx = 0x11,
    HxOpcodeHf15693Scan = 0x20,
    HxOpcodeHf15693TxRx = 0x21,
    HxOpcodeLfReadDecoded = 0x30,
    HxOpcodeAbort = 0x7f,
} HxOpcode;

typedef enum {
    HxStatusOk = 0x00,
    HxStatusUnsupported = 0x01,
    HxStatusInvalidRequest = 0x02,
    HxStatusBusy = 0x03,
    HxStatusIoError = 0x04,
    HxStatusTimeout = 0x05,
    HxStatusNotFound = 0x06,
} HxStatus;

typedef struct {
    uint8_t opcode;
    const uint8_t* body;
    size_t body_len;
} HxRequest;

typedef enum {
    HxFrameStateNeedMore = 0,
    HxFrameStateReady,
    HxFrameStateDesync,
    HxFrameStateInvalidLength,
} HxFrameState;

uint8_t hx_calc_lrc(const uint8_t* data, size_t len);
bool hx_validate_lrc(const uint8_t* data, size_t len);
size_t hx_add_lrc(uint8_t* data, size_t len);

HxFrameState hx_ccid_frame_state(
    const uint8_t* data,
    size_t len,
    size_t max_frame_len,
    size_t* frame_len);
bool hx_ccid_is_escape_frame(const uint8_t* frame, size_t frame_len);
bool hx_parse_request_payload(const uint8_t* payload, size_t payload_len, HxRequest* request);
bool hx_copy_response_body(
    const uint8_t* body,
    size_t body_len,
    uint8_t* out,
    size_t out_cap,
    size_t* out_len);
size_t hx_build_hf14a_scan_payload(
    const uint8_t* uid,
    size_t uid_len,
    const uint8_t atqa[2],
    uint8_t sak,
    const uint8_t* ats,
    size_t ats_len,
    uint8_t* out,
    size_t out_cap);
size_t hx_build_response_payload(
    uint8_t opcode,
    uint8_t status,
    const uint8_t* body,
    size_t body_len,
    uint8_t* out,
    size_t out_cap);
size_t hx_build_ccid_escape_response(
    const uint8_t* request_frame,
    size_t request_frame_len,
    const uint8_t* payload,
    size_t payload_len,
    uint8_t* out,
    size_t out_cap);
