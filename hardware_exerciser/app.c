#include "protocol.h"

#include <cli/cli_vcp.h>
#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_usb_cdc.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <input/input.h>
#include <lib/lfrfid/lfrfid_worker.h>
#include <lib/nfc/nfc.h>
#include <lib/nfc/nfc_device.h>
#include <lfrfid/protocols/lfrfid_protocols.h>
#include <nfc/nfc_poller.h>
#include <toolbox/protocols/protocol_dict.h>
#include <lib/nfc/protocols/iso14443_3a/iso14443_3a.h>
#include <lib/nfc/protocols/iso14443_3a/iso14443_3a_poller.h>
#include <lib/nfc/protocols/iso14443_3a/iso14443_3a_poller_sync.h>
#include <lib/nfc/protocols/iso14443_4a/iso14443_4a.h>
#include <lib/nfc/protocols/iso14443_4a/iso14443_4a_poller.h>
#include <lib/nfc/protocols/iso15693_3/iso15693_3.h>
#include <lib/nfc/protocols/iso15693_3/iso15693_3_poller.h>
#include <lib/toolbox/simple_array.h>

/*
 * HardwareExerciser is a dual bridge:
 * - CDC0 terminates a small protocol carried inside CCID escape frames and forwards non-escape
 *   traffic to the SAM UART.
 * - CDC1 is a raw passthrough to the UHF UART.
 *
 * The app keeps protocol framing in this file and isolates wire helpers in protocol.c so host
 * tooling and host tests can validate the message layout independently from the firmware runtime.
 */
#define TAG "HardwareExerciser"

#define HE_BAUDRATE 115200U
#define HE_UART_RX_BUF_SIZE (CDC_DATA_SZ * 64U)
#define HE_CDC_ACC_BUF_SIZE 1024U
#define HE_ESCAPE_BODY_MAX 512U
#define HE_WAIT_TIMEOUT_MS 1000U
#define HE_14A_ATS_MAX 64U

/* Cross-thread events shared by the per-bridge worker thread and its CDC RX helper thread. */
typedef enum {
    HeWorkerEvtStop = (1 << 0),
    HeWorkerEvtRxDone = (1 << 1),
    HeWorkerEvtTxStop = (1 << 2),
    HeWorkerEvtCdcRx = (1 << 3),
    HeWorkerEvtCdcTxComplete = (1 << 4),
    HeWorkerEvtSessionReset = (1 << 5),
} HeWorkerEvt;

#define HE_WORKER_RX_EVENTS \
    (HeWorkerEvtStop | HeWorkerEvtRxDone | HeWorkerEvtCdcTxComplete | HeWorkerEvtSessionReset)
#define HE_WORKER_TX_EVENTS (HeWorkerEvtTxStop | HeWorkerEvtCdcRx | HeWorkerEvtSessionReset)

typedef enum {
    HeBridgeModeSamEscape = 0,
    HeBridgeModeUhfRaw,
} HeBridgeMode;

typedef enum {
    HeBridgeInitStatusOk = 0,
    HeBridgeInitStatusAllocFailed,
    HeBridgeInitStatusSerialBusy,
} HeBridgeInitStatus;

typedef struct HardwareExerciserApp HardwareExerciserApp;

/*
 * Per-bridge runtime state.
 *
 * The worker thread owns UART RX processing and sends data back to USB. The TX thread drains
 * host-side CDC RX so parsing and forwarding do not block the UART side of the bridge.
 */
typedef struct {
    HardwareExerciserApp* app;
    HeBridgeMode mode;
    uint8_t vcp_ch;
    uint8_t uart_ch;
    FuriThread* thread;
    FuriThreadId worker_thread_id;
    FuriThread* tx_thread;
    FuriThreadId tx_thread_id;
    FuriStreamBuffer* rx_stream;
    FuriSemaphore* tx_sem;
    FuriMutex* usb_mutex;
    FuriHalSerialHandle* serial_handle;
    bool cdc_connected;
    bool cdc_callbacks_registered;
    FuriEventFlag* init_event;
    HeBridgeInitStatus init_status;
    uint8_t cdc_accum[HE_CDC_ACC_BUF_SIZE];
    size_t cdc_accum_len;
    uint8_t uart_rx_buf[CDC_DATA_SZ];
} HeBridge;

/* App-wide state shared between the two bridges and the simple status UI. */
struct HardwareExerciserApp {
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* event_queue;
    CliVcp* cli_vcp;
    FuriHalUsbInterface* usb_prev;
    FuriMutex* hw_mutex;
    FuriMutex* usb_mutex;
    bool startup_failed;
    HeBridge sam_bridge;
    HeBridge uhf_bridge;
};

typedef struct {
    InputEvent input;
} HeAppEvent;

/* Context used while upgrading a 14A scan into ISO14443-4A ATS collection. */
typedef struct {
    FuriEventFlag* event;
    NfcPoller* poller;
    bool found;
    uint8_t uid[ISO14443_3A_MAX_UID_SIZE];
    size_t uid_len;
    uint8_t atqa[2];
    uint8_t sak;
    uint8_t ats[HE_14A_ATS_MAX];
    size_t ats_len;
} He14aScanContext;

/* Context for one-shot 14A transceive requests driven by the HX protocol. */
typedef struct {
    FuriEventFlag* event;
    NfcPoller* poller;
    const uint8_t* tx_data;
    size_t tx_len;
    uint8_t rx_data[HE_ESCAPE_BODY_MAX];
    size_t rx_len;
    HxStatus status;
} He14aTxRxContext;

/* Context for one-shot ISO15693 inventory/system-info reads. */
typedef struct {
    FuriEventFlag* event;
    NfcPoller* poller;
    bool found;
    uint8_t uid[ISO15693_3_UID_SIZE];
    size_t uid_len;
    Iso15693_3SystemInfo info;
} He15693ScanContext;

/* Context for one-shot ISO15693 transceive requests with caller-supplied FWT. */
typedef struct {
    FuriEventFlag* event;
    NfcPoller* poller;
    const uint8_t* tx_data;
    size_t tx_len;
    uint32_t fwt_fc;
    uint8_t rx_data[HE_ESCAPE_BODY_MAX];
    size_t rx_len;
    HxStatus status;
} He15693TxRxContext;

/* LF worker callback only needs to return the detected protocol id and completion signal. */
typedef struct {
    FuriEventFlag* event;
    ProtocolId protocol;
} HeLfReadContext;

/* Capability mask advertised by GetCaps. Keep this aligned with the dispatch table below. */
static const uint32_t he_caps =
    HX_CAP_PING | HX_CAP_GET_CAPS | HX_CAP_GET_STATUS | HX_CAP_HF14A_SCAN | HX_CAP_HF14A_TXRX |
    HX_CAP_HF15693_SCAN | HX_CAP_HF15693_TXRX | HX_CAP_LF_READ_DECODED;

#define HE_BRIDGE_INIT_READY_FLAG (1U << 0)

static const char* he_bridge_init_status_label(HeBridgeInitStatus status) {
    if(status == HeBridgeInitStatusSerialBusy) {
        return "UART busy";
    } else if(status == HeBridgeInitStatusAllocFailed) {
        return "alloc failed";
    }

    return "ready";
}

static void he_view_draw_callback(Canvas* canvas, void* context) {
    const HardwareExerciserApp* app = context;

    /* The UI is intentionally tiny: it only confirms bridge role, connection state, and failures. */
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "HardwareExerciser");
    if(app->startup_failed) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 2, 24, "Startup failed");
        if(app->sam_bridge.init_status != HeBridgeInitStatusOk) {
            canvas_draw_str(canvas, 2, 38, "SAM:");
            canvas_draw_str(canvas, 30, 38, he_bridge_init_status_label(app->sam_bridge.init_status));
        }
        if(app->uhf_bridge.init_status != HeBridgeInitStatusOk) {
            canvas_draw_str(canvas, 2, 50, "UHF:");
            canvas_draw_str(canvas, 30, 50, he_bridge_init_status_label(app->uhf_bridge.init_status));
        }
        canvas_draw_str(canvas, 2, 63, "Back to exit");
        return;
    }
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 24, "CDC0: SAM + Escape");
    canvas_draw_str(canvas, 2, 34, "CDC1: UHF Raw");
    canvas_draw_str(canvas, 2, 44, "115200 baud");
    canvas_draw_str(canvas, 2, 54, app->sam_bridge.cdc_connected ? "SAM host: connected" : "SAM host: idle");
    canvas_draw_str(canvas, 2, 63, app->uhf_bridge.cdc_connected ? "UHF host: connected" : "UHF host: idle");
}

static void he_view_input_callback(InputEvent* input_event, void* context) {
    HeAppEvent event = {.input = *input_event};
    furi_message_queue_put((FuriMessageQueue*)context, &event, FuriWaitForever);
}

static void he_bridge_release_tx_slot(HeBridge* bridge) {
    /* CDC writes are single-flight; disconnect/reset paths may need to unblock a waiting sender. */
    if(furi_semaphore_get_count(bridge->tx_sem) == 0U) {
        furi_semaphore_release(bridge->tx_sem);
    }
}

static void he_bridge_reset_cdc_accum(HeBridge* bridge) {
    bridge->cdc_accum_len = 0U;
    memset(bridge->cdc_accum, 0, sizeof(bridge->cdc_accum));
}

static void he_bridge_reset_session(HeBridge* bridge) {
    /* Drop partial host frames and stale UART bytes whenever the USB session is torn down. */
    he_bridge_reset_cdc_accum(bridge);
    if(bridge->rx_stream) {
        furi_check(furi_stream_buffer_reset(bridge->rx_stream) == FuriStatusOk);
    }
}

static void he_bridge_signal_worker(HeBridge* bridge, uint32_t flags) {
    if(bridge->worker_thread_id) {
        furi_thread_flags_set(bridge->worker_thread_id, flags);
    }
}

static void he_bridge_signal_tx(HeBridge* bridge, uint32_t flags) {
    if(bridge->tx_thread_id) {
        furi_thread_flags_set(bridge->tx_thread_id, flags);
    }
}

static void he_bridge_on_uart_rx_dma(
    FuriHalSerialHandle* handle,
    FuriHalSerialRxEvent event,
    size_t size,
    void* context) {
    HeBridge* bridge = context;

    if(event & (FuriHalSerialRxEventData | FuriHalSerialRxEventIdle)) {
        uint8_t data[FURI_HAL_SERIAL_DMA_BUFFER_SIZE] = {0};

        /* Drain all bytes the DMA engine reports before waking the bridge worker. */
        while(size > 0U) {
            const size_t request_size =
                size > FURI_HAL_SERIAL_DMA_BUFFER_SIZE ? FURI_HAL_SERIAL_DMA_BUFFER_SIZE : size;
            const size_t received = furi_hal_serial_dma_rx(handle, data, request_size);
            if(received == 0U) {
                break;
            }

            furi_stream_buffer_send(bridge->rx_stream, data, received, 0U);
            size -= received;
        }

        he_bridge_signal_worker(bridge, HeWorkerEvtRxDone);
    }
}

static void he_bridge_vcp_tx_complete(void* context) {
    HeBridge* bridge = context;
    he_bridge_release_tx_slot(bridge);
    he_bridge_signal_worker(bridge, HeWorkerEvtCdcTxComplete);
}

static void he_bridge_vcp_rx(void* context) {
    HeBridge* bridge = context;
    he_bridge_signal_tx(bridge, HeWorkerEvtCdcRx);
}

static void he_bridge_vcp_state(void* context, CdcState state) {
    HeBridge* bridge = context;
    bridge->cdc_connected = (state == CdcStateConnected);
    if(state == CdcStateDisconnected) {
        /* A disappearing host must not leave partial frames or a blocked sender behind. */
        he_bridge_release_tx_slot(bridge);
        he_bridge_signal_worker(bridge, HeWorkerEvtSessionReset);
        he_bridge_signal_tx(bridge, HeWorkerEvtSessionReset);
    }
    view_port_update(bridge->app->view_port);
    he_bridge_signal_worker(bridge, HeWorkerEvtCdcTxComplete);
}

static void he_bridge_vcp_ctrl_line(void* context, CdcCtrlLine state) {
    UNUSED(context);
    UNUSED(state);
}

static void he_bridge_vcp_line_config(void* context, struct usb_cdc_line_coding* config) {
    UNUSED(context);
    UNUSED(config);
}

static const CdcCallbacks he_bridge_cdc_callbacks = {
    .tx_ep_callback = he_bridge_vcp_tx_complete,
    .rx_ep_callback = he_bridge_vcp_rx,
    .state_callback = he_bridge_vcp_state,
    .ctrl_line_callback = he_bridge_vcp_ctrl_line,
    .config_callback = he_bridge_vcp_line_config,
};

static bool he_bridge_send_cdc(HeBridge* bridge, const uint8_t* data, size_t len) {
    if(!bridge->cdc_connected || len == 0U) {
        return false;
    }

    /* The Flipper CDC API is asynchronous; use a semaphore so only one transfer is in flight. */
    if(furi_semaphore_acquire(bridge->tx_sem, HE_WAIT_TIMEOUT_MS) != FuriStatusOk) {
        return false;
    }

    /* Both bridges share the USB device, so guard callback registration and endpoint access. */
    furi_check(furi_mutex_acquire(bridge->usb_mutex, FuriWaitForever) == FuriStatusOk);
    furi_hal_cdc_send(bridge->vcp_ch, (uint8_t*)data, len);
    furi_check(furi_mutex_release(bridge->usb_mutex) == FuriStatusOk);
    return true;
}

static bool he_bridge_forward_frame_to_uart(HeBridge* bridge, const uint8_t* data, size_t len) {
    if(!bridge->serial_handle || len == 0U) {
        return false;
    }

    furi_hal_serial_tx(bridge->serial_handle, data, len);
    return true;
}

static void he_bridge_forward_uart_to_cdc(HeBridge* bridge) {
    /* Keep draining UART RX until CDC backpressure or an empty stream says to stop. */
    while(bridge->cdc_connected) {
        const size_t len = furi_stream_buffer_receive(
            bridge->rx_stream, bridge->uart_rx_buf, sizeof(bridge->uart_rx_buf), 0U);

        if(len == 0U) {
            break;
        }

        if(!he_bridge_send_cdc(bridge, bridge->uart_rx_buf, len)) {
            break;
        }
    }
}

static size_t he_flatten_14a_ats(const Iso14443_4aData* iso4, uint8_t* out, size_t out_cap) {
    size_t ats_len = 0U;
    uint32_t tk_count = 0U;

    if(!iso4 || !out || out_cap == 0U || iso4->ats_data.tl <= 1U) {
        return 0U;
    }

    if(iso4->ats_data.t1_tk != NULL) {
        tk_count = simple_array_get_count(iso4->ats_data.t1_tk);
    }

    if(out_cap < 4U + tk_count) {
        return 0U;
    }

    /* Rebuild ATS bytes from the parsed SDK representation so the host sees a compact blob. */
    out[ats_len++] = iso4->ats_data.t0;
    if(iso4->ats_data.t0 & (1U << 4)) out[ats_len++] = iso4->ats_data.ta_1;
    if(iso4->ats_data.t0 & (1U << 5)) out[ats_len++] = iso4->ats_data.tb_1;
    if(iso4->ats_data.t0 & (1U << 6)) out[ats_len++] = iso4->ats_data.tc_1;
    if(tk_count > 0U) {
        const uint8_t* tk = simple_array_cget_data(iso4->ats_data.t1_tk);
        memcpy(out + ats_len, tk, tk_count);
        ats_len += tk_count;
    }

    return ats_len;
}

static HxStatus he_map_14a_error(Iso14443_3aError error) {
    if(error == Iso14443_3aErrorNone) {
        return HxStatusOk;
    } else if(error == Iso14443_3aErrorTimeout) {
        return HxStatusTimeout;
    } else if(error == Iso14443_3aErrorNotPresent) {
        return HxStatusNotFound;
    }

    return HxStatusIoError;
}

static NfcCommand he_14a_ats_callback(NfcGenericEvent event, void* context) {
    He14aScanContext* scan = context;
    const Iso14443_4aPollerEvent* iso_event = event.event_data;

    /* The callback is only used to opportunistically collect ATS after a successful 14A scan. */
    if(event.protocol == NfcProtocolIso14443_4a && iso_event &&
       iso_event->type == Iso14443_4aPollerEventTypeReady) {
        const Iso14443_4aData* data = nfc_poller_get_data(scan->poller);
        if(data) {
            const uint8_t* uid = iso14443_4a_get_uid(data, &scan->uid_len);
            if(uid && scan->uid_len <= sizeof(scan->uid)) {
                memcpy(scan->uid, uid, scan->uid_len);
                scan->ats_len = he_flatten_14a_ats(data, scan->ats, sizeof(scan->ats));
            }
        }
    }

    furi_event_flag_set(scan->event, 1U);
    return NfcCommandStop;
}

static NfcCommand he_14a_txrx_callback(NfcGenericEvent event, void* context) {
    He14aTxRxContext* txrx = context;
    const Iso14443_3aPollerEvent* iso_event = event.event_data;
    BitBuffer* tx_buffer = NULL;
    BitBuffer* rx_buffer = NULL;

    /* A ready event means the poller is active and a tag is present for a single exchange. */
    if(event.protocol != NfcProtocolIso14443_3a || !iso_event ||
       iso_event->type != Iso14443_3aPollerEventTypeReady) {
        txrx->status = HxStatusNotFound;
        furi_event_flag_set(txrx->event, 1U);
        return NfcCommandStop;
    }

    tx_buffer = bit_buffer_alloc(txrx->tx_len);
    rx_buffer = bit_buffer_alloc(HE_ESCAPE_BODY_MAX);
    if(!tx_buffer || !rx_buffer) {
        txrx->status = HxStatusIoError;
        goto done;
    }

    bit_buffer_append_bytes(tx_buffer, txrx->tx_data, txrx->tx_len);
    const Iso14443_3aError error = iso14443_3a_poller_txrx(
        (Iso14443_3aPoller*)event.instance, tx_buffer, rx_buffer, ISO14443_3A_FDT_LISTEN_FC);
    if(error == Iso14443_3aErrorNone) {
        /* Copy out of BitBuffer before freeing it so the synchronous caller owns the response. */
        txrx->rx_len = bit_buffer_get_size_bytes(rx_buffer);
        if(txrx->rx_len <= sizeof(txrx->rx_data)) {
            memcpy(txrx->rx_data, bit_buffer_get_data(rx_buffer), txrx->rx_len);
            txrx->status = HxStatusOk;
        } else {
            txrx->status = HxStatusIoError;
        }
    } else {
        txrx->status = he_map_14a_error(error);
    }

done:
    if(tx_buffer) bit_buffer_free(tx_buffer);
    if(rx_buffer) bit_buffer_free(rx_buffer);
    furi_event_flag_set(txrx->event, 1U);
    return NfcCommandStop;
}

static NfcCommand he_15693_scan_callback(NfcGenericEvent event, void* context) {
    He15693ScanContext* scan = context;
    const Iso15693_3PollerEvent* iso_event = event.event_data;

    /* ISO15693 scan piggybacks on the poller-ready callback to snapshot UID and system info. */
    if(event.protocol == NfcProtocolIso15693_3 && iso_event &&
       iso_event->type == Iso15693_3PollerEventTypeReady) {
        const Iso15693_3Data* data = nfc_poller_get_data(scan->poller);
        if(data) {
            const uint8_t* uid = iso15693_3_get_uid(data, &scan->uid_len);
            if(uid && scan->uid_len <= sizeof(scan->uid)) {
                memcpy(scan->uid, uid, scan->uid_len);
                scan->info = data->system_info;
                scan->found = true;
            }
        }
    }

    furi_event_flag_set(scan->event, 1U);
    return NfcCommandStop;
}

static NfcCommand he_15693_txrx_callback(NfcGenericEvent event, void* context) {
    He15693TxRxContext* txrx = context;
    const Iso15693_3PollerEvent* iso_event = event.event_data;
    BitBuffer* tx_buffer = NULL;
    BitBuffer* rx_buffer = NULL;

    /* The HX request already carries the raw frame, so only timing and copy-out happen here. */
    if(event.protocol != NfcProtocolIso15693_3 || !iso_event ||
       iso_event->type != Iso15693_3PollerEventTypeReady) {
        txrx->status = HxStatusNotFound;
        furi_event_flag_set(txrx->event, 1U);
        return NfcCommandStop;
    }

    tx_buffer = bit_buffer_alloc(txrx->tx_len);
    rx_buffer = bit_buffer_alloc(HE_ESCAPE_BODY_MAX);
    if(!tx_buffer || !rx_buffer) {
        txrx->status = HxStatusIoError;
        goto done;
    }

    bit_buffer_append_bytes(tx_buffer, txrx->tx_data, txrx->tx_len);
    const Iso15693_3Error error = iso15693_3_poller_send_frame(
        (Iso15693_3Poller*)event.instance, tx_buffer, rx_buffer, txrx->fwt_fc);
    if(error == Iso15693_3ErrorNone) {
        txrx->rx_len = bit_buffer_get_size_bytes(rx_buffer);
        if(txrx->rx_len <= sizeof(txrx->rx_data)) {
            memcpy(txrx->rx_data, bit_buffer_get_data(rx_buffer), txrx->rx_len);
            txrx->status = HxStatusOk;
        } else {
            txrx->status = HxStatusIoError;
        }
    } else if(error == Iso15693_3ErrorTimeout) {
        txrx->status = HxStatusTimeout;
    } else {
        txrx->status = HxStatusIoError;
    }

done:
    if(tx_buffer) bit_buffer_free(tx_buffer);
    if(rx_buffer) bit_buffer_free(rx_buffer);
    furi_event_flag_set(txrx->event, 1U);
    return NfcCommandStop;
}

static void he_lf_read_callback(LFRFIDWorkerReadResult result, ProtocolId proto, void* context) {
    HeLfReadContext* lf = context;
    if(result == LFRFIDWorkerReadDone) {
        lf->protocol = proto;
    }
    /* Signal the exact LF worker outcome so the synchronous caller can time out or continue. */
    furi_event_flag_set(lf->event, 1U << result);
}

static HxStatus
    he_handle_ping(const HxRequest* request, uint8_t* out, size_t out_cap, size_t* out_len) {
    /* Ping is a transport sanity check, so it echoes the request body verbatim. */
    return hx_copy_response_body(request->body, request->body_len, out, out_cap, out_len) ?
               HxStatusOk :
               HxStatusInvalidRequest;
}

static HxStatus he_handle_get_caps(uint8_t* out, size_t out_cap, size_t* out_len) {
    if(out_cap < 4U) {
        *out_len = 0U;
        return HxStatusIoError;
    }
    /* Capabilities are returned little-endian to match the protocol helper and host tooling. */
    out[0] = (uint8_t)(he_caps & 0xffU);
    out[1] = (uint8_t)((he_caps >> 8) & 0xffU);
    out[2] = (uint8_t)((he_caps >> 16) & 0xffU);
    out[3] = (uint8_t)((he_caps >> 24) & 0xffU);
    *out_len = 4U;
    return HxStatusOk;
}

static HxStatus
    he_handle_get_status(HardwareExerciserApp* app, uint8_t* out, size_t out_cap, size_t* out_len) {
    if(out_cap < 4U) {
        *out_len = 0U;
        return HxStatusIoError;
    }
    /* Report host connection state separately from UART ownership so setup failures are visible. */
    out[0] = app->sam_bridge.cdc_connected ? 1U : 0U;
    out[1] = app->uhf_bridge.cdc_connected ? 1U : 0U;
    out[2] = app->sam_bridge.serial_handle ? 1U : 0U;
    out[3] = app->uhf_bridge.serial_handle ? 1U : 0U;
    *out_len = 4U;
    return HxStatusOk;
}

static HxStatus
    he_handle_hf14a_scan(HardwareExerciserApp* app, uint8_t* out, size_t out_cap, size_t* out_len) {
    Nfc* nfc = NULL;
    NfcPoller* poller = NULL;
    FuriEventFlag* event = NULL;
    He14aScanContext scan = {0};
    HxStatus status = HxStatusIoError;
    Iso14443_3aData iso3_data = {};
    Iso14443_3aError iso3_error;

    UNUSED(app);
    /* First do a synchronous 14A identify pass, then optionally re-enter as 4A to collect ATS. */
    nfc = nfc_alloc();
    if(!nfc) {
        goto cleanup;
    }

    iso3_error = iso14443_3a_poller_sync_read(nfc, &iso3_data);
    status = he_map_14a_error(iso3_error);
    if(status != HxStatusOk) {
        goto cleanup;
    }

    const uint8_t* uid = iso14443_3a_get_uid(&iso3_data, &scan.uid_len);
    if(!uid || scan.uid_len > sizeof(scan.uid)) {
        status = HxStatusIoError;
        goto cleanup;
    }
    memcpy(scan.uid, uid, scan.uid_len);
    iso14443_3a_get_atqa(&iso3_data, scan.atqa);
    scan.sak = iso14443_3a_get_sak(&iso3_data);
    scan.found = true;

    if(iso14443_3a_supports_iso14443_4(&iso3_data)) {
        scan.ats_len = 0U;
        poller = nfc_poller_alloc(nfc, NfcProtocolIso14443_4a);
        event = furi_event_flag_alloc();
        if(!poller || !event) {
            status = HxStatusIoError;
            goto cleanup;
        }

        scan.event = event;
        scan.poller = poller;
        nfc_poller_start(poller, he_14a_ats_callback, &scan);
        const uint32_t flags = furi_event_flag_wait(event, 1U, FuriFlagWaitAny, HE_WAIT_TIMEOUT_MS);
        nfc_poller_stop(poller);
        if(flags == FuriFlagErrorTimeout) {
            /* ATS is optional for scan success; timeout falls back to UID/ATQA/SAK only. */
            scan.ats_len = 0U;
        }
    }

    const size_t response_len = hx_build_hf14a_scan_payload(
        scan.uid, scan.uid_len, scan.atqa, scan.sak, scan.ats, scan.ats_len, out, out_cap);
    if(response_len == 0U) {
        status = HxStatusIoError;
        goto cleanup;
    }
    *out_len = response_len;
    status = HxStatusOk;

cleanup:
    if(event) furi_event_flag_free(event);
    if(poller) nfc_poller_free(poller);
    if(nfc) nfc_free(nfc);
    return status;
}

static HxStatus
    he_handle_hf14a_txrx(
        HardwareExerciserApp* app,
        const HxRequest* request,
        uint8_t* out,
        size_t out_cap,
        size_t* out_len) {
    Nfc* nfc = NULL;
    NfcPoller* poller = NULL;
    FuriEventFlag* event = NULL;
    He14aTxRxContext txrx = {0};
    HxStatus status = HxStatusIoError;

    UNUSED(app);
    if(request->body_len == 0U) {
        /* The host must provide at least one raw frame byte to transmit. */
        return HxStatusInvalidRequest;
    }

    nfc = nfc_alloc();
    if(!nfc) {
        goto cleanup;
    }

    poller = nfc_poller_alloc(nfc, NfcProtocolIso14443_3a);
    event = furi_event_flag_alloc();
    if(!poller || !event) {
        goto cleanup;
    }

    txrx.event = event;
    txrx.poller = poller;
    txrx.tx_data = request->body;
    txrx.tx_len = request->body_len;
    txrx.status = HxStatusNotFound;
    /* The callback executes exactly one exchange and signals completion back through event flags. */
    nfc_poller_start(poller, he_14a_txrx_callback, &txrx);
    const uint32_t flags = furi_event_flag_wait(event, 1U, FuriFlagWaitAny, HE_WAIT_TIMEOUT_MS);
    nfc_poller_stop(poller);
    if(flags == FuriFlagErrorTimeout) {
        status = HxStatusTimeout;
        goto cleanup;
    }

    if(txrx.status == HxStatusOk) {
        if(!hx_copy_response_body(txrx.rx_data, txrx.rx_len, out, out_cap, out_len)) {
            status = HxStatusIoError;
            goto cleanup;
        }
    }
    status = txrx.status;

cleanup:
    if(event) furi_event_flag_free(event);
    if(poller) nfc_poller_free(poller);
    if(nfc) nfc_free(nfc);
    return status;
}

static HxStatus
    he_handle_hf15693_scan(HardwareExerciserApp* app, uint8_t* out, size_t out_cap, size_t* out_len) {
    Nfc* nfc = NULL;
    NfcPoller* poller = NULL;
    FuriEventFlag* event = NULL;
    He15693ScanContext scan = {0};
    HxStatus status = HxStatusIoError;

    UNUSED(app);
    nfc = nfc_alloc();
    if(!nfc) {
        goto cleanup;
    }

    poller = nfc_poller_alloc(nfc, NfcProtocolIso15693_3);
    event = furi_event_flag_alloc();
    if(!poller || !event) {
        goto cleanup;
    }

    scan.event = event;
    scan.poller = poller;
    /* The poller callback snapshots the discovered tag and system-info fields into scan. */
    nfc_poller_start(poller, he_15693_scan_callback, &scan);
    const uint32_t flags = furi_event_flag_wait(event, 1U, FuriFlagWaitAny, HE_WAIT_TIMEOUT_MS);
    nfc_poller_stop(poller);
    if(flags == FuriFlagErrorTimeout) {
        status = HxStatusTimeout;
        goto cleanup;
    }
    if(!scan.found) {
        status = HxStatusNotFound;
        goto cleanup;
    }

    const size_t response_len = 5U + scan.uid_len;
    if(response_len > out_cap) {
        status = HxStatusIoError;
        goto cleanup;
    }

    out[0] = (uint8_t)scan.uid_len;
    memcpy(out + 1U, scan.uid, scan.uid_len);
    out[1U + scan.uid_len] = scan.info.flags;
    out[2U + scan.uid_len] = scan.info.block_size;
    out[3U + scan.uid_len] = (uint8_t)(scan.info.block_count & 0xffU);
    out[4U + scan.uid_len] = (uint8_t)((scan.info.block_count >> 8) & 0xffU);
    *out_len = response_len;
    status = HxStatusOk;

cleanup:
    if(event) furi_event_flag_free(event);
    if(poller) nfc_poller_free(poller);
    if(nfc) nfc_free(nfc);
    return status;
}

static HxStatus
    he_handle_hf15693_txrx(
        HardwareExerciserApp* app,
        const HxRequest* request,
        uint8_t* out,
        size_t out_cap,
        size_t* out_len) {
    Nfc* nfc = NULL;
    NfcPoller* poller = NULL;
    FuriEventFlag* event = NULL;
    He15693TxRxContext txrx = {0};
    HxStatus status = HxStatusIoError;

    UNUSED(app);
    if(request->body_len < 5U) {
        /* Body is encoded as fwt_fc_le32 followed by at least one outbound frame byte. */
        return HxStatusInvalidRequest;
    }

    nfc = nfc_alloc();
    if(!nfc) {
        goto cleanup;
    }

    poller = nfc_poller_alloc(nfc, NfcProtocolIso15693_3);
    event = furi_event_flag_alloc();
    if(!poller || !event) {
        goto cleanup;
    }

    txrx.event = event;
    txrx.poller = poller;
    txrx.fwt_fc = ((uint32_t)request->body[0]) | ((uint32_t)request->body[1] << 8) |
                  ((uint32_t)request->body[2] << 16) | ((uint32_t)request->body[3] << 24);
    txrx.tx_data = request->body + 4U;
    txrx.tx_len = request->body_len - 4U;
    txrx.status = HxStatusNotFound;
    nfc_poller_start(poller, he_15693_txrx_callback, &txrx);
    const uint32_t flags = furi_event_flag_wait(event, 1U, FuriFlagWaitAny, HE_WAIT_TIMEOUT_MS);
    nfc_poller_stop(poller);
    if(flags == FuriFlagErrorTimeout) {
        status = HxStatusTimeout;
        goto cleanup;
    }

    if(txrx.status == HxStatusOk) {
        if(!hx_copy_response_body(txrx.rx_data, txrx.rx_len, out, out_cap, out_len)) {
            status = HxStatusIoError;
            goto cleanup;
        }
    }
    status = txrx.status;

cleanup:
    if(event) furi_event_flag_free(event);
    if(poller) nfc_poller_free(poller);
    if(nfc) nfc_free(nfc);
    return status;
}

static HxStatus
    he_handle_lf_read_decoded(HardwareExerciserApp* app, uint8_t* out, size_t out_cap, size_t* out_len) {
    ProtocolDict* dict = NULL;
    LFRFIDWorker* worker = NULL;
    FuriEventFlag* event = NULL;
    HeLfReadContext context = {0};
    FuriString* rendered = NULL;
    uint8_t* protocol_data = NULL;
    HxStatus status = HxStatusIoError;

    UNUSED(app);
    dict = protocol_dict_alloc(lfrfid_protocols, LFRFIDProtocolMax);
    worker = dict ? lfrfid_worker_alloc(dict) : NULL;
    event = furi_event_flag_alloc();
    if(!dict || !worker || !event) {
        goto cleanup;
    }

    context.event = event;
    context.protocol = PROTOCOL_NO;
    lfrfid_worker_start_thread(worker);
    /* LF decode runs asynchronously and returns the matched protocol plus rendered protocol data. */
    lfrfid_worker_read_start(worker, LFRFIDWorkerReadTypeAuto, he_lf_read_callback, &context);
    const uint32_t flags =
        furi_event_flag_wait(event, 1U << LFRFIDWorkerReadDone, FuriFlagWaitAny, HE_WAIT_TIMEOUT_MS);
    lfrfid_worker_stop(worker);
    lfrfid_worker_stop_thread(worker);
    if(flags == FuriFlagErrorTimeout) {
        status = HxStatusTimeout;
        goto cleanup;
    }
    if(context.protocol == PROTOCOL_NO) {
        status = HxStatusNotFound;
        goto cleanup;
    }

    const char* name = protocol_dict_get_name(dict, context.protocol);
    const size_t name_len = strlen(name);
    const size_t data_len = protocol_dict_get_data_size(dict, context.protocol);
    rendered = furi_string_alloc();
    protocol_data = malloc(data_len);
    if(!rendered || !protocol_data || name_len > 255U || data_len > 255U) {
        goto cleanup;
    }

    protocol_dict_get_data(dict, context.protocol, protocol_data, data_len);
    protocol_dict_render_data(dict, rendered, context.protocol);
    const char* rendered_text = furi_string_get_cstr(rendered);
    const size_t rendered_len = strlen(rendered_text);
    if(rendered_len > 255U) {
        goto cleanup;
    }

    const size_t response_len = 4U + name_len + data_len + rendered_len;
    if(response_len > out_cap) {
        goto cleanup;
    }

    out[0] = (uint8_t)context.protocol;
    out[1] = (uint8_t)name_len;
    out[2] = (uint8_t)data_len;
    out[3] = (uint8_t)rendered_len;
    /* Pack name, binary data, and rendered text into one self-describing response blob. */
    memcpy(out + 4U, name, name_len);
    memcpy(out + 4U + name_len, protocol_data, data_len);
    memcpy(out + 4U + name_len + data_len, rendered_text, rendered_len);
    *out_len = response_len;
    status = HxStatusOk;

cleanup:
    if(protocol_data) free(protocol_data);
    if(rendered) furi_string_free(rendered);
    if(event) furi_event_flag_free(event);
    if(worker) lfrfid_worker_free(worker);
    if(dict) protocol_dict_free(dict);
    return status;
}

static HxStatus he_dispatch_request(
    HardwareExerciserApp* app,
    const HxRequest* request,
    uint8_t* out,
    size_t out_cap,
    size_t* out_len) {
    *out_len = 0U;

    /* Cheap metadata opcodes bypass the hardware mutex so they are always available. */
    if(request->opcode == HxOpcodePing) {
        return he_handle_ping(request, out, out_cap, out_len);
    } else if(request->opcode == HxOpcodeGetCaps) {
        return he_handle_get_caps(out, out_cap, out_len);
    } else if(request->opcode == HxOpcodeGetStatus) {
        return he_handle_get_status(app, out, out_cap, out_len);
    } else if(request->opcode == HxOpcodeAbort) {
        return HxStatusOk;
    }

    if(furi_mutex_acquire(app->hw_mutex, 0U) != FuriStatusOk) {
        /* Hardware-facing opcodes are intentionally single-flight across both bridges. */
        return HxStatusBusy;
    }

    HxStatus status = HxStatusUnsupported;
    if(request->opcode == HxOpcodeHf14aScan) {
        status = he_handle_hf14a_scan(app, out, out_cap, out_len);
    } else if(request->opcode == HxOpcodeHf14aTxRx) {
        status = he_handle_hf14a_txrx(app, request, out, out_cap, out_len);
    } else if(request->opcode == HxOpcodeHf15693Scan) {
        status = he_handle_hf15693_scan(app, out, out_cap, out_len);
    } else if(request->opcode == HxOpcodeHf15693TxRx) {
        status = he_handle_hf15693_txrx(app, request, out, out_cap, out_len);
    } else if(request->opcode == HxOpcodeLfReadDecoded) {
        status = he_handle_lf_read_decoded(app, out, out_cap, out_len);
    }

    furi_mutex_release(app->hw_mutex);
    return status;
}

static bool he_bridge_handle_escape_frame(HeBridge* bridge, const uint8_t* frame, size_t frame_len) {
    const size_t payload_len =
        ((size_t)frame[3]) | ((size_t)frame[4] << 8) | ((size_t)frame[5] << 16) | ((size_t)frame[6] << 24);
    const uint8_t* payload = frame + 12U;
    HxRequest request = {0};
    uint8_t response_body[HE_ESCAPE_BODY_MAX];
    size_t response_body_len = 0U;
    uint8_t response_payload[HE_ESCAPE_BODY_MAX + 8U];
    uint8_t response_frame[HE_ESCAPE_BODY_MAX + 32U];
    uint8_t opcode = 0U;
    HxStatus status;

    if(payload_len >= 4U) {
        opcode = payload[3];
    }

    /* Malformed frames still get a structured response when the opcode byte can be recovered. */
    if(!hx_validate_lrc(frame, frame_len) || !hx_parse_request_payload(payload, payload_len, &request)) {
        status = HxStatusInvalidRequest;
    } else {
        opcode = request.opcode;
        status = he_dispatch_request(
            bridge->app, &request, response_body, sizeof(response_body), &response_body_len);
    }

    size_t payload_size = hx_build_response_payload(
        opcode, status, response_body, response_body_len, response_payload, sizeof(response_payload));
    if(payload_size == 0U) {
        /* If response assembly fails, fall back to a minimal IoError envelope. */
        response_body_len = 0U;
        payload_size = hx_build_response_payload(
            opcode, HxStatusIoError, NULL, 0U, response_payload, sizeof(response_payload));
    }

    const size_t frame_size = payload_size == 0U ?
                                  0U :
                                  hx_build_ccid_escape_response(
                                      frame,
                                      frame_len,
                                      response_payload,
                                      payload_size,
                                      response_frame,
                                      sizeof(response_frame));
    return frame_size > 0U && he_bridge_send_cdc(bridge, response_frame, frame_size);
}

static void he_bridge_handle_cdc_rx(HeBridge* bridge) {
    while(1) {
        size_t received = 0U;

        /* Pull as much host data as is currently available into the accumulation buffer. */
        while(bridge->cdc_accum_len < sizeof(bridge->cdc_accum)) {
            const size_t remaining = sizeof(bridge->cdc_accum) - bridge->cdc_accum_len;
            const size_t request_size = remaining > CDC_DATA_SZ ? CDC_DATA_SZ : remaining;

            furi_check(furi_mutex_acquire(bridge->usb_mutex, FuriWaitForever) == FuriStatusOk);
            const int32_t len =
                furi_hal_cdc_receive(bridge->vcp_ch, bridge->cdc_accum + bridge->cdc_accum_len, request_size);
            furi_check(furi_mutex_release(bridge->usb_mutex) == FuriStatusOk);

            if(len <= 0) {
                break;
            }

            bridge->cdc_accum_len += (size_t)len;
            received += (size_t)len;
        }

        if(received == 0U && bridge->cdc_accum_len == 0U) {
            break;
        }

        if(bridge->mode == HeBridgeModeUhfRaw) {
            if(bridge->cdc_accum_len > 0U) {
                /* CDC1 never parses frames; every byte goes straight to the UHF UART. */
                he_bridge_forward_frame_to_uart(bridge, bridge->cdc_accum, bridge->cdc_accum_len);
                bridge->cdc_accum_len = 0U;
            }
            if(received == 0U) break;
            continue;
        }

        while(bridge->cdc_accum_len > 0U) {
            size_t frame_len = 0U;
            const HxFrameState state =
                hx_ccid_frame_state(
                    bridge->cdc_accum,
                    bridge->cdc_accum_len,
                    sizeof(bridge->cdc_accum),
                    &frame_len);

            if(state == HxFrameStateDesync || state == HxFrameStateInvalidLength) {
                /* Resynchronize one byte at a time until the framing preamble is found again. */
                memmove(bridge->cdc_accum, bridge->cdc_accum + 1U, bridge->cdc_accum_len - 1U);
                bridge->cdc_accum_len--;
                continue;
            }

            if(state == HxFrameStateNeedMore) {
                if(bridge->cdc_accum_len == sizeof(bridge->cdc_accum)) {
                    /* Full buffer with no complete frame means the oldest byte cannot be trusted. */
                    memmove(bridge->cdc_accum, bridge->cdc_accum + 1U, bridge->cdc_accum_len - 1U);
                    bridge->cdc_accum_len--;
                    continue;
                }
                break;
            }

            if(hx_ccid_is_escape_frame(bridge->cdc_accum, frame_len)) {
                /* Escape frames terminate locally and become HX protocol operations. */
                he_bridge_handle_escape_frame(bridge, bridge->cdc_accum, frame_len);
            } else {
                /* Ordinary CCID traffic is transparent and continues to the SAM UART. */
                he_bridge_forward_frame_to_uart(bridge, bridge->cdc_accum, frame_len);
            }

            memmove(bridge->cdc_accum, bridge->cdc_accum + frame_len, bridge->cdc_accum_len - frame_len);
            bridge->cdc_accum_len -= frame_len;
        }

        if(received == 0U) {
            break;
        }
    }
}

static int32_t he_bridge_tx_thread(void* context) {
    HeBridge* bridge = context;
    bridge->tx_thread_id = furi_thread_get_current_id();

    /* This thread exists solely to react to host RX callbacks without blocking the worker thread. */
    while(1) {
        const uint32_t events =
            furi_thread_flags_wait(HE_WORKER_TX_EVENTS, FuriFlagWaitAny, FuriWaitForever);

        if(events & HeWorkerEvtTxStop) {
            break;
        }

        if(events & HeWorkerEvtSessionReset) {
            he_bridge_reset_cdc_accum(bridge);
        }

        if(events & HeWorkerEvtCdcRx) {
            he_bridge_handle_cdc_rx(bridge);
        }
    }

    bridge->tx_thread_id = NULL;
    return 0;
}

static int32_t he_bridge_worker(void* context) {
    HeBridge* bridge = context;

    bridge->worker_thread_id = furi_thread_get_current_id();
    bridge->init_status = HeBridgeInitStatusAllocFailed;
    bridge->rx_stream = furi_stream_buffer_alloc(HE_UART_RX_BUF_SIZE, 1U);
    bridge->tx_sem = furi_semaphore_alloc(1U, 1U);
    bridge->tx_thread = furi_thread_alloc_ex("HeBridgeTx", 1024U, he_bridge_tx_thread, bridge);
    bridge->serial_handle = furi_hal_serial_control_acquire(bridge->uart_ch);
    if(!bridge->rx_stream || !bridge->tx_sem || !bridge->usb_mutex || !bridge->tx_thread) {
        bridge->init_status = HeBridgeInitStatusAllocFailed;
        goto init_failed;
    }
    if(!bridge->serial_handle) {
        bridge->init_status = HeBridgeInitStatusSerialBusy;
        goto init_failed;
    }

    /* The UART side must be live before CDC callbacks are installed and allowed to enqueue work. */
    furi_hal_serial_init(bridge->serial_handle, HE_BAUDRATE);
    furi_thread_start(bridge->tx_thread);
    furi_hal_serial_dma_rx_start(bridge->serial_handle, he_bridge_on_uart_rx_dma, bridge, false);
    furi_hal_cdc_set_callbacks(bridge->vcp_ch, (CdcCallbacks*)&he_bridge_cdc_callbacks, bridge);
    bridge->cdc_callbacks_registered = true;
    he_bridge_signal_tx(bridge, HeWorkerEvtCdcRx);
    bridge->init_status = HeBridgeInitStatusOk;
    furi_event_flag_set(bridge->init_event, HE_BRIDGE_INIT_READY_FLAG);

    while(1) {
        const uint32_t events =
            furi_thread_flags_wait(HE_WORKER_RX_EVENTS, FuriFlagWaitAny, FuriWaitForever);

        if(events & HeWorkerEvtStop) {
            break;
        }

        if(events & HeWorkerEvtSessionReset) {
            he_bridge_reset_session(bridge);
        }

        if(events & (HeWorkerEvtRxDone | HeWorkerEvtCdcTxComplete)) {
            /* New UART bytes or a completed USB transfer both unblock more UART->CDC forwarding. */
            he_bridge_forward_uart_to_cdc(bridge);
        }
    }

    /* Teardown order matters: stop callbacks and TX thread before releasing UART resources. */
    if(bridge->cdc_callbacks_registered) {
        furi_hal_cdc_set_callbacks(bridge->vcp_ch, NULL, NULL);
        bridge->cdc_callbacks_registered = false;
    }
    bridge->cdc_connected = false;
    he_bridge_release_tx_slot(bridge);
    if(bridge->tx_thread) {
        he_bridge_signal_tx(bridge, HeWorkerEvtTxStop);
        furi_thread_join(bridge->tx_thread);
        furi_thread_free(bridge->tx_thread);
        bridge->tx_thread = NULL;
        bridge->tx_thread_id = NULL;
    }
    if(bridge->serial_handle) {
        furi_hal_serial_deinit(bridge->serial_handle);
        furi_hal_serial_control_release(bridge->serial_handle);
        bridge->serial_handle = NULL;
    }
    if(bridge->rx_stream) {
        furi_stream_buffer_free(bridge->rx_stream);
        bridge->rx_stream = NULL;
    }
    if(bridge->tx_sem) {
        furi_semaphore_free(bridge->tx_sem);
        bridge->tx_sem = NULL;
    }
    bridge->worker_thread_id = NULL;
    return 0;

init_failed:
    /* Mirror the normal cleanup path so partial startup cannot leak callbacks or UART ownership. */
    if(bridge->cdc_callbacks_registered) {
        furi_hal_cdc_set_callbacks(bridge->vcp_ch, NULL, NULL);
        bridge->cdc_callbacks_registered = false;
    }
    bridge->cdc_connected = false;
    furi_event_flag_set(bridge->init_event, HE_BRIDGE_INIT_READY_FLAG);
    if(bridge->tx_thread) {
        furi_thread_free(bridge->tx_thread);
        bridge->tx_thread = NULL;
        bridge->tx_thread_id = NULL;
    }
    if(bridge->serial_handle) {
        furi_hal_serial_control_release(bridge->serial_handle);
        bridge->serial_handle = NULL;
    }
    if(bridge->rx_stream) {
        furi_stream_buffer_free(bridge->rx_stream);
        bridge->rx_stream = NULL;
    }
    if(bridge->tx_sem) {
        furi_semaphore_free(bridge->tx_sem);
        bridge->tx_sem = NULL;
    }
    bridge->worker_thread_id = NULL;
    return -1;
}

static bool he_bridge_start(HeBridge* bridge) {
    bridge->init_event = furi_event_flag_alloc();
    bridge->thread = furi_thread_alloc_ex("HeBridge", 2048U, he_bridge_worker, bridge);
    if(!bridge->init_event || !bridge->thread) {
        bridge->init_status = HeBridgeInitStatusAllocFailed;
        if(bridge->thread) {
            furi_thread_free(bridge->thread);
            bridge->thread = NULL;
        }
        if(bridge->init_event) {
            furi_event_flag_free(bridge->init_event);
            bridge->init_event = NULL;
        }
        return false;
    }
    furi_thread_set_priority(bridge->thread, FuriThreadPriorityHigh);
    furi_thread_start(bridge->thread);
    /* Startup is synchronous from the app's perspective: wait for the worker to report readiness. */
    furi_event_flag_wait(
        bridge->init_event, HE_BRIDGE_INIT_READY_FLAG, FuriFlagWaitAny, FuriWaitForever);
    furi_event_flag_free(bridge->init_event);
    bridge->init_event = NULL;
    if(bridge->init_status != HeBridgeInitStatusOk) {
        furi_thread_join(bridge->thread);
        furi_thread_free(bridge->thread);
        bridge->thread = NULL;
        return false;
    }
    return true;
}

static void he_bridge_stop(HeBridge* bridge) {
    if(!bridge->thread) {
        return;
    }
    he_bridge_signal_worker(bridge, HeWorkerEvtStop);
    furi_thread_join(bridge->thread);
    furi_thread_free(bridge->thread);
    bridge->thread = NULL;
}

static HardwareExerciserApp* he_app_alloc(void) {
    HardwareExerciserApp* app = calloc(1U, sizeof(HardwareExerciserApp));
    if(!app) {
        return NULL;
    }

    app->gui = furi_record_open(RECORD_GUI);
    app->cli_vcp = furi_record_open(RECORD_CLI_VCP);
    app->event_queue = furi_message_queue_alloc(8U, sizeof(HeAppEvent));
    app->view_port = view_port_alloc();
    app->hw_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->usb_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    if(!app->event_queue || !app->view_port || !app->hw_mutex || !app->usb_mutex) {
        if(app->view_port) view_port_free(app->view_port);
        if(app->event_queue) furi_message_queue_free(app->event_queue);
        if(app->hw_mutex) furi_mutex_free(app->hw_mutex);
        if(app->usb_mutex) furi_mutex_free(app->usb_mutex);
        furi_record_close(RECORD_CLI_VCP);
        furi_record_close(RECORD_GUI);
        free(app);
        return NULL;
    }

    view_port_draw_callback_set(app->view_port, he_view_draw_callback, app);
    view_port_input_callback_set(app->view_port, he_view_input_callback, app->event_queue);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    /* CDC0 fronts the SAM path and handles local escape requests. */
    app->sam_bridge.app = app;
    app->sam_bridge.mode = HeBridgeModeSamEscape;
    app->sam_bridge.vcp_ch = 0U;
    app->sam_bridge.uart_ch = FuriHalSerialIdLpuart;
    app->sam_bridge.usb_mutex = app->usb_mutex;

    /* CDC1 is intentionally simpler: it is a raw host-to-UHF serial bridge. */
    app->uhf_bridge.app = app;
    app->uhf_bridge.mode = HeBridgeModeUhfRaw;
    app->uhf_bridge.vcp_ch = 1U;
    app->uhf_bridge.uart_ch = FuriHalSerialIdUsart;
    app->uhf_bridge.usb_mutex = app->usb_mutex;
    return app;
}

static void he_app_free(HardwareExerciserApp* app) {
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_message_queue_free(app->event_queue);
    furi_mutex_free(app->hw_mutex);
    furi_mutex_free(app->usb_mutex);
    furi_record_close(RECORD_CLI_VCP);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t hardware_exerciser_app(void* p) {
    UNUSED(p);

    HardwareExerciserApp* app = he_app_alloc();
    if(!app) {
        return 255;
    }
    app->usb_prev = furi_hal_usb_get_config();
    furi_hal_usb_unlock();
    cli_vcp_disable(app->cli_vcp);
    /* Switch the device into dual CDC mode so both bridges are available to the host at once. */
    furi_check(furi_hal_usb_set_config(&usb_cdc_dual, NULL) == true);

    const bool sam_started = he_bridge_start(&app->sam_bridge);
    const bool uhf_started = he_bridge_start(&app->uhf_bridge);
    if(!sam_started || !uhf_started) {
        /* Fall back to a status screen instead of exiting immediately so startup errors are visible. */
        he_bridge_stop(&app->uhf_bridge);
        he_bridge_stop(&app->sam_bridge);
        furi_hal_usb_set_config(app->usb_prev, NULL);
        cli_vcp_enable(app->cli_vcp);
        app->startup_failed = true;
        view_port_update(app->view_port);
    }

    while(1) {
        HeAppEvent event = {0};
        if(furi_message_queue_get(app->event_queue, &event, 100U) == FuriStatusOk) {
            if(event.input.type == InputTypeLong && event.input.key == InputKeyBack) {
                break;
            }
        }
    }

    if(!app->startup_failed) {
        /* Restore the original USB configuration and CLI only after both bridges are stopped. */
        he_bridge_stop(&app->uhf_bridge);
        he_bridge_stop(&app->sam_bridge);
        furi_hal_usb_set_config(app->usb_prev, NULL);
        cli_vcp_enable(app->cli_vcp);
    }
    he_app_free(app);
    return 0;
}
