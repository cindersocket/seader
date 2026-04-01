# Hardware Exerciser

`HardwareExerciser` exposes two USB CDC channels that let a host drive attached reader hardware without entering the full Seader application flow.

- `CDC0`: SAM bridge plus protocol-aware CCID escape handling.
- `CDC1`: raw UHF UART passthrough.

The implementation lives in `hardware_exerciser/app.c` and the shared wire helpers live in `hardware_exerciser/protocol.{h,c}`.

## Runtime Model

The app starts two independent bridge instances:

- A SAM bridge that can either forward ordinary CCID traffic to the SAM UART or intercept `PC_TO_RDR_ESCAPE` frames that carry Hardware Exerciser requests.
- A UHF bridge that simply shuttles bytes between `CDC1` and the UHF UART.

Each bridge owns:

- one worker thread for UART RX handling and session lifecycle,
- one TX thread for draining host-side CDC RX,
- one CDC accumulation buffer for host frames,
- one UART RX stream buffer for DMA-fed bytes waiting to be sent back to USB.

Only one hardware operation is allowed at a time. NFC and LF opcodes are serialized behind the shared app-level hardware mutex so overlapping scans or transactions cannot race the radio stack.

When a CDC client disconnects, the bridge resets its session state:

- partially accumulated host frames are discarded,
- buffered UART RX data is dropped,
- waiting TX slots are released so the bridge cannot deadlock on a vanished host.

## Protocol Overview

Hardware Exerciser requests are carried inside a CCID escape frame on `CDC0`.

### Request Frame Layout

1. Wire preamble:
   - byte `0`: `HX_SYNC` (`0x03`)
   - byte `1`: `HX_CTRL` (`0x06`)
2. CCID header:
   - byte `2`: `PC_TO_RDR_ESCAPE` (`0x6B`)
   - bytes `3..6`: little-endian payload length
   - byte `7`: slot
   - byte `8`: sequence
   - bytes `9..11`: reserved, sent as zero
3. Hardware Exerciser payload:
   - byte `0`: `'H'`
   - byte `1`: `'X'`
   - byte `2`: protocol version, currently `1`
   - byte `3`: opcode
   - bytes `4..n`: opcode-specific body
4. Final byte: LRC over the entire frame except the LRC byte itself

### Response Frame Layout

Responses mirror the incoming transport envelope:

- the response uses `RDR_TO_PC_ESCAPE` (`0x83`),
- the request slot and sequence are copied into the response,
- the payload starts with `HX`, version, opcode, and one status byte,
- the trailing LRC covers the entire outbound frame.

Response payload body layout is:

- byte `0`: `'H'`
- byte `1`: `'X'`
- byte `2`: protocol version
- byte `3`: opcode echoed from the request
- byte `4`: `HxStatus`
- bytes `5..n`: optional opcode-specific body

## Opcodes

| Opcode | Name | Request Body | Response Body |
| --- | --- | --- | --- |
| `0x01` | `Ping` | Arbitrary bytes | Echo of request body |
| `0x02` | `GetCaps` | None | 32-bit little-endian capability mask |
| `0x03` | `GetStatus` | None | `sam_cdc_connected`, `uhf_cdc_connected`, `sam_uart_ready`, `uhf_uart_ready` |
| `0x10` | `Hf14aScan` | None | `uid_len`, `uid`, `atqa_lo`, `atqa_hi`, `sak`, `ats_len`, `ats` |
| `0x11` | `Hf14aTxRx` | Raw ISO14443A frame bytes | Raw response frame bytes |
| `0x20` | `Hf15693Scan` | None | `uid_len`, `uid`, `flags`, `block_size`, `block_count_le16` |
| `0x21` | `Hf15693TxRx` | `fwt_fc_le32` followed by raw frame bytes | Raw response frame bytes |
| `0x30` | `LfReadDecoded` | None | `protocol_id`, `name_len`, `data_len`, `rendered_len`, then `name`, `protocol_data`, `rendered_text` |
| `0x7f` | `Abort` | None | Empty success response |

## Status Codes

| Value | Name | Meaning |
| --- | --- | --- |
| `0x00` | `Ok` | Request completed successfully |
| `0x01` | `Unsupported` | Opcode is known to the protocol layer but not implemented in the app |
| `0x02` | `InvalidRequest` | Malformed frame, bad magic/version, or request body does not match opcode expectations |
| `0x03` | `Busy` | Another hardware operation already holds the app-level hardware mutex |
| `0x04` | `IoError` | Allocation failure, transport error, or response buffer construction failure |
| `0x05` | `Timeout` | Operation did not complete before the app timeout |
| `0x06` | `NotFound` | No target card/tag or protocol-specific item was found |

## Capability Bits

`GetCaps` returns a 32-bit mask. The current app advertises:

- `HX_CAP_PING`
- `HX_CAP_GET_CAPS`
- `HX_CAP_GET_STATUS`
- `HX_CAP_HF14A_SCAN`
- `HX_CAP_HF14A_TXRX`
- `HX_CAP_HF15693_SCAN`
- `HX_CAP_HF15693_TXRX`
- `HX_CAP_LF_READ_DECODED`

## Host Helper

`tools/hardware_exerciser_capture.py` is a minimal manual probe for `CDC0`.

- It builds a single CCID escape request frame.
- It writes the frame to the selected serial port.
- It waits for a full response header and payload.
- It prints the raw response frame as hex.
