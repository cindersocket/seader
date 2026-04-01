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

/* Opcodes carried in byte 3 of the HX payload header. */
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
