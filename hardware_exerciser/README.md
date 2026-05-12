# Hardware Exerciser

`HardwareExerciser` exposes USB CDC channels that let a host drive attached reader hardware without entering the full Seader application flow.

- `CDC0`: SAM bridge plus protocol-aware CCID escape handling.
- `CDC1`: raw UHF UART passthrough when a UHF module is detected, or when the host explicitly forces the bridge on for characterization.

The implementation lives in `hardware_exerciser/app.c` and the shared wire helpers live in `hardware_exerciser/protocol.{h,c}`.

## Runtime Model

The app starts the SAM bridge first, then probes the UHF side before deciding whether to expose a live UHF raw bridge:

- A SAM bridge that can either forward ordinary CCID traffic to the SAM UART or intercept `PC_TO_RDR_ESCAPE` frames that carry Hardware Exerciser requests.
- A UHF bridge that shuttles bytes between `CDC1` and the UHF UART only after a module answers the version probe, or after `UhfBridgeControl(force)`.

On boards without a detected UHF module, the UI reports `CDC1: no UHF` and the app leaves the UHF raw bridge stopped. UHF probing temporarily enables 5V OTG and `PC3`, sends M100/QM100 version requests at 115200 baud, and powers the rail back down when no module answers.

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
| `0x03` | `GetStatus` | None | `sam_cdc_connected`, `uhf_cdc_connected`, `sam_uart_ready`, `uhf_uart_ready`, `uhf_presence`, `uhf_bridge_enabled`, `uhf_bridge_forced`, `uhf_power_enabled` |
| `0x04` | `GetBoardSnapshot` | None | `board_class`, `pa4_high`, `pc1_high`, `pc0_high`, `pc3_level`, `otg_enabled`, `otg_fault`, reserved, `vbus_mv_le16` |
| `0x10` | `Hf14aScan` | None | `uid_len`, `uid`, `atqa_lo`, `atqa_hi`, `sak`, `ats_len`, `ats` |
| `0x11` | `Hf14aTxRx` | Raw ISO14443A frame bytes | Raw response frame bytes |
| `0x20` | `Hf15693Scan` | None | `uid_len`, `uid`, `flags`, `block_size`, `block_count_le16` |
| `0x21` | `Hf15693TxRx` | `fwt_fc_le32` followed by raw frame bytes | Raw response frame bytes |
| `0x30` | `LfReadDecoded` | None | `protocol_id`, `name_len`, `data_len`, `rendered_len`, then `name`, `protocol_data`, `rendered_text` |
| `0x40` | `GpioListPins` | None | `pin_count`, then repeated `pin_number`, `flags`, `name_len`, `name` |
| `0x41` | `GpioConfigure` | `pin_number`, `mode`, `pull`, `initial_level` | Empty success response |
| `0x42` | `GpioWrite` | `pin_number`, `level` | Empty success response |
| `0x43` | `GpioRead` | `pin_number` | `level` |
| `0x44` | `GpioReadAnalog` | `pin_number` | `raw_adc_le16`, `millivolts_le16` |
| `0x45` | `GpioVectorCapture` | `write_count`, writes, `settle_us_le32`, `sample_count`, samples | `sample_count`, then repeated `pin_number`, `sample_kind`, `value_le16`, `aux_le16` |
| `0x46` | `GpioReset` | None | Empty success response |
| `0x50` | `UhfPower` | `action` | `power_enabled`, `otg_enabled`, `otg_fault`, reserved, `vbus_mv_le16` |
| `0x51` | `UhfProbe` | None | `presence`, `family`, `hardware_class`, `hw_len`, `hw_version`, `sw_len`, `sw_version` |
| `0x52` | `UhfBridgeControl` | `action` | `bridge_enabled`, `forced`, `module_present` |
| `0x53` | `UhfNo5vTest` | None | `overall`, board/power summary, module summary, saved flag |
| `0x7e` | `Quit` | None | Empty success response |
| `0x7f` | `Abort` | None | Empty body with `Unsupported` status |

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
- `HX_CAP_GPIO_LIST_PINS`
- `HX_CAP_GPIO_CONFIGURE`
- `HX_CAP_GPIO_WRITE`
- `HX_CAP_GPIO_READ`
- `HX_CAP_GPIO_READ_ANALOG`
- `HX_CAP_GPIO_VECTOR`
- `HX_CAP_GPIO_RESET`
- `HX_CAP_QUIT`
- `HX_CAP_BOARD_SNAPSHOT`
- `HX_CAP_UHF_POWER`
- `HX_CAP_UHF_PROBE`
- `HX_CAP_UHF_BRIDGE`
- `HX_CAP_UHF_NO5V_TEST`

## UHF Characterization

UHF-related fields use small numeric enums so host scripts can log them cheaply:

- board class: `0=unknown`, `1=none`, `2=SAM-only`, `3=UHF carrier`
- module presence: `0=unknown`, `1=absent`, `2=present`
- module family: `0=unknown`, `1=M100`, `2=QM100`
- hardware class: `0=unknown`, `1=20dBm`, `2=26dBm`, `3=30dBm`
- UHF power action: `0=off`, `1=on`, `2=hibernate`
- UHF bridge action: `0=disable`, `1=enable-if-present`, `2=force-enable`
- UHF no-5V power sequence: `0=OTG then PC3`, `1=PC3 then OTG`
- UHF exchange status: `0=not-run`, `1=ok`, `2=no-rx`, `3=bad-frame`, `4=wrong-frame`, `5=timeout`

`GetBoardSnapshot` samples the hardware strap pins with pulldowns. `PA4` high identifies a UHF carrier; otherwise `PC1` or `PC0` high identifies a SAM-only board. The snapshot also returns the current `PC3` enable level, OTG state, OTG fault state, and VBUS voltage in millivolts.

`UhfProbe` temporarily stops the raw UHF bridge if it is running, powers the module rail, asks for
hardware and software version strings, classifies the result, and restarts the bridge when
appropriate. The normal probe is conservative for battery operation: it confirms the UHF carrier
strap first, drives `PC3` low, disables OTG, releases external GPIOs, waits 1000 ms, enables OTG,
waits 1000 ms, asserts `PC3`, waits 500 ms, and then reads the module version with a 500 ms timeout
and up to three attempts. A missing module returns `NotFound` with an absent probe payload.

At startup, Hardware Exerciser automatically runs that safe probe. If the module is found, the main
screen shows the UHF hardware version and starts the raw CDC1 UHF bridge. If no UHF carrier/module
is detected, the screen shows `UHF: not detected` and CDC1 remains disabled.

`UhfBridgeControl(enable)` starts the raw bridge only when a module is currently present or can be detected. `UhfBridgeControl(force)` powers the UHF rail and starts the raw bridge even without a successful probe, which is intended for bring-up and failure characterization only.

`UhfNo5vTest` is the same bounded characterization that the on-device OK button starts. It first
powers the UHF rail down and snapshots the carrier with `PC3=0`. If VBUS is already at least
4500 mV, the result is marked as external 5V present so it cannot be mistaken for no-supply data.
Otherwise the app enables OTG, asserts `PC3`, waits for settle, reads module versions, retries with
the alternate power sequence if the first version request times out, sends RF off/on/off and one
single-poll command, hibernates the module, powers down, and saves
`/ext/apps_data/hardware_exerciser/uhf_no5v_last.txt`. The report includes the selected power
sequence, OTG/PC3 snapshots, version exchange status, RX byte count, frame counters, and the first
captured RX bytes. Battery-only `VBUS mV` can remain zero even while OTG reports enabled, so UART
response and RX evidence are the decisive module-power indicators.

## GPIO Details

GPIO requests target safe external connector pins only. Host requests identify a pin by connector number, while `GpioListPins` returns the corresponding firmware name such as `PA7` or `PC3`.

`GpioListPins` flags currently mean:

- bit `0`: digital IO supported
- bit `1`: analog sampling supported

GPIO modes are encoded as:

- `0`: input
- `1`: output push-pull
- `2`: output open-drain
- `3`: analog

GPIO pulls are encoded as:

- `0`: none
- `1`: pull-up
- `2`: pull-down

Vector capture request bodies are laid out as:

- `write_count u8`
- repeated writes: `pin_number u8 | level u8`
- `settle_us u32le`
- `sample_count u8`
- repeated samples: `pin_number u8 | sample_kind u8`

Vector sample kinds are:

- `0`: digital sample, returned as `value=0/1`, `aux=0`
- `1`: analog sample, returned as `value=raw_adc`, `aux=millivolts`

## Host Helper

`tools/hardware_exerciser_capture.py` is a minimal manual probe for `CDC0`.

- It builds a single CCID escape request frame.
- It writes the frame to the selected serial port.
- It waits for a full response header and payload.
- It prints the raw response frame as hex.
- It supports convenience subcommands for `status`, `board-snapshot`, `list-pins`, `configure`, `write`, `read`, `read-analog`, `vector`, `reset`, `uhf-power`, `uhf-probe`, `uhf-bridge`, `uhf-no5v-test`, and `quit`, plus a generic `raw` mode for arbitrary opcodes.
