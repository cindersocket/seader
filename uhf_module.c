#ifndef ASN_EMIT_DEBUG
#define ASN_EMIT_DEBUG 0
#endif

#include "uhf_module.h"

#include "uhf_frame.h"
#include "uhf_logic.h"
#include "uhf_session_lifecycle.h"
#include "uhf_uart.h"

#ifdef SEADER_UHF_DEBUG
#define UHF_DIAG_I(...) FURI_LOG_I(TAG, __VA_ARGS__)
#define UHF_DIAG_W(...) FURI_LOG_W(TAG, __VA_ARGS__)
#else
#define UHF_DIAG_I(...) \
    do {                \
    } while(0)
#define UHF_DIAG_W(...) \
    do {                \
    } while(0)
#endif

#define UHF_PD_STAGE(result_ptr, stage_code, fmt, ...)                      \
    do {                                                                    \
        (result_ptr)->debug_stage = (uint16_t)(stage_code);                 \
        UHF_DIAG_I("pd[%04x] " fmt, (unsigned)(stage_code), ##__VA_ARGS__); \
    } while(0)

#define TAG                             "SeaderUHF"
#define SEADER_UHF_UART_ID              FuriHalSerialIdUsart
#define SEADER_UHF_UART_BAUDRATE        115200U
#define SEADER_UHF_RX_MAX               96U
#define SEADER_UHF_PROBE_TIMEOUT_MS     220U
#define SEADER_UHF_PROBE_STEP_MS        5U
#define SEADER_UHF_POWER_SETTLE_MS      250U
#define SEADER_UHF_ENABLE_SETTLE_MS     150U
#define SEADER_UHF_CLOSE_SETTLE_MS      20U
#define SEADER_UHF_DETECT_ATTEMPTS      8U
#define SEADER_UHF_DETECT_RETRY_MS      150U
#define SEADER_UHF_BUS_ADDRESS          0x2EU
#define SEADER_UHF_WORDS_TID_READ       0x000CU
#define SEADER_UHF_WORDS_USER_READ      0x0020U
#define SEADER_UHF_PRIVATE_WAIT_MS      3000U
#define SEADER_UHF_PRIVATE_WAIT_STEP_MS 75U
#define SEADER_UHF_PRIVATE_TOTAL_MS     4000U
#define SEADER_UHF_REGION_US            0x02U
#define SEADER_UHF_CHANNEL_US_0         0x00U

struct SeaderUhf {
    SeaderUhfUart uart;
    SeaderUhfModulePresence presence;
    SeaderUhfModuleProfile profile;
    SeaderUhfTagSnapshot snapshot;
    bool power_enabled;
    bool power_owned;
};

static const uint8_t seader_uhf_cmd_hw_version[] = {0xBB, 0x00, 0x03, 0x00, 0x01, 0x00, 0x04, 0x7E};
static const uint8_t seader_uhf_cmd_single_poll[] = {0xBB, 0x00, 0x22, 0x00, 0x00, 0x22, 0x7E};
static const uint8_t seader_uhf_cmd_hibernate[] = {0xBB, 0x00, 0x17, 0x00, 0x00, 0x17, 0x7E};
static const uint8_t seader_uhf_cmd_rf_off[] = {0xBB, 0x00, 0xB0, 0x00, 0x01, 0x00, 0xB1, 0x7E};
static const uint8_t seader_uhf_cmd_rf_on[] = {0xBB, 0x00, 0xB0, 0x00, 0x01, 0xFF, 0xB0, 0x7E};
static const uint16_t seader_uhf_qt_public_mask = 0x4000U;
static const uint8_t seader_uhf_version_raw[] = "M100";

static void seader_uhf_power_off(SeaderUhf* uhf);
static bool seader_uhf_parse_single_poll(
    const uint8_t* payload,
    size_t payload_len,
    uint8_t* epc,
    size_t epc_cap,
    size_t* epc_len);

static uint16_t seader_uhf_vbus_mv_from_volts(float vbus_voltage) {
    if(vbus_voltage <= 0.0f) {
        return 0U;
    }

    const float scaled = vbus_voltage * 1000.0f;
    if(scaled >= (float)UINT16_MAX) {
        return UINT16_MAX;
    }

    return (uint16_t)(scaled + 0.5f);
}

static void seader_uhf_set_enable_pin(bool enabled) {
    furi_hal_gpio_init(&gpio_ext_pc3, GpioModeOutputPushPull, GpioPullNo, GpioSpeedLow);
    furi_hal_gpio_write(&gpio_ext_pc3, enabled);
}

static void seader_uhf_reset_runtime_state(SeaderUhf* uhf) {
    if(!uhf) return;
    memset(&uhf->snapshot, 0, sizeof(uhf->snapshot));
}

static bool seader_uhf_send_expect(
    SeaderUhf* uhf,
    const uint8_t* tx,
    size_t tx_len,
    uint8_t expected_type,
    uint8_t expected_cmd,
    uint8_t* payload,
    size_t payload_cap,
    size_t* payload_len) {
    if(!uhf || !tx || tx_len == 0U || !payload || !payload_len) return false;

    seader_uhf_uart_rx_reset(&uhf->uart);
    if(!seader_uhf_uart_tx(&uhf->uart, tx, tx_len)) return false;

    uint32_t waited = 0U;
    while(waited <= SEADER_UHF_PROBE_TIMEOUT_MS) {
        if(seader_uhf_try_parse_frame(
               uhf->uart.rx.stream,
               uhf->uart.rx.stream_len,
               expected_type,
               expected_cmd,
               payload,
               payload_cap,
               payload_len)) {
            return true;
        }
        furi_delay_ms(SEADER_UHF_PROBE_STEP_MS);
        waited += SEADER_UHF_PROBE_STEP_MS;
    }

    return false;
}

static bool seader_uhf_send_custom_expect(
    SeaderUhf* uhf,
    uint8_t command,
    const uint8_t* command_payload,
    size_t command_payload_len,
    uint8_t expected_type,
    uint8_t expected_cmd,
    uint8_t* payload,
    size_t payload_cap,
    size_t* payload_len) {
    uint8_t frame[80] = {0};
    size_t frame_len = 0U;
    if(!seader_uhf_build_frame(
           command, command_payload, command_payload_len, frame, sizeof(frame), &frame_len)) {
        return false;
    }
    return seader_uhf_send_expect(
        uhf, frame, frame_len, expected_type, expected_cmd, payload, payload_cap, payload_len);
}

static bool seader_uhf_send_simple_ack(
    SeaderUhf* uhf,
    uint8_t command,
    const uint8_t* command_payload,
    size_t command_payload_len) {
    uint8_t payload[SEADER_UHF_RX_MAX] = {0};
    size_t payload_len = 0U;
    return seader_uhf_send_custom_expect(
               uhf,
               command,
               command_payload,
               command_payload_len,
               0x01U,
               command,
               payload,
               sizeof(payload),
               &payload_len) &&
           payload_len >= 1U && payload[0] == 0x00U;
}

static bool seader_uhf_open_session(SeaderUhf* uhf) {
    return seader_uhf_uart_open(&uhf->uart, SEADER_UHF_UART_ID, uhf->profile.baudrate);
}

static void seader_uhf_close_session(SeaderUhf* uhf, bool hibernate) {
    uint8_t payload[SEADER_UHF_RX_MAX] = {0};
    size_t payload_len = 0U;

    if(!uhf) return;
    if(uhf->uart.handle) {
        (void)seader_uhf_send_expect(
            uhf,
            seader_uhf_cmd_rf_off,
            sizeof(seader_uhf_cmd_rf_off),
            0x01U,
            0xB0U,
            payload,
            sizeof(payload),
            &payload_len);
        furi_delay_ms(SEADER_UHF_CLOSE_SETTLE_MS);
        if(hibernate) {
            (void)seader_uhf_send_expect(
                uhf,
                seader_uhf_cmd_hibernate,
                sizeof(seader_uhf_cmd_hibernate),
                0x01U,
                0x17U,
                payload,
                sizeof(payload),
                &payload_len);
            furi_delay_ms(SEADER_UHF_CLOSE_SETTLE_MS);
        }
    }
    seader_uhf_uart_close(&uhf->uart);
}

static bool seader_uhf_power_on(SeaderUhf* uhf) {
    if(!uhf) return false;
    if(uhf->power_enabled) {
        seader_uhf_set_enable_pin(true);
        return true;
    }

    const bool already_on = furi_hal_power_is_otg_enabled();
    const uint16_t vbus_mv = seader_uhf_vbus_mv_from_volts(furi_hal_power_get_usb_voltage());
    const SeaderUhfPowerAcquirePlan power_plan =
        seader_uhf_plan_power_acquire(already_on, vbus_mv);
    if(power_plan.should_enable_otg) {
        if(!furi_hal_power_enable_otg()) {
            UHF_DIAG_W("power_on: otg enable failed");
            return false;
        }
        uhf->power_owned = power_plan.owns_otg;
    } else {
        uhf->power_owned = false;
    }
    furi_delay_ms(SEADER_UHF_POWER_SETTLE_MS);
    const bool otg_enabled = furi_hal_power_is_otg_enabled();
    const uint16_t settled_vbus_mv =
        seader_uhf_vbus_mv_from_volts(furi_hal_power_get_usb_voltage());
    if(!seader_uhf_power_is_available(otg_enabled, settled_vbus_mv)) {
        UHF_DIAG_W(
            "power_on: power unavailable after settle otg=%d vbus=%u",
            otg_enabled,
            settled_vbus_mv);
        seader_uhf_power_off(uhf);
        return false;
    }
    if(seader_uhf_power_fault_is_fatal(furi_hal_power_check_otg_fault(), settled_vbus_mv)) {
        UHF_DIAG_W("power_on: otg fault after settle");
        seader_uhf_power_off(uhf);
        return false;
    }
    seader_uhf_set_enable_pin(true);
    uhf->power_enabled = true;
    furi_delay_ms(SEADER_UHF_ENABLE_SETTLE_MS);
    return true;
}

static void seader_uhf_power_off(SeaderUhf* uhf) {
    if(!uhf) return;

    seader_uhf_close_session(uhf, true);
    seader_uhf_set_enable_pin(false);
    if(seader_uhf_should_disable_owned_otg(uhf->power_owned, furi_hal_power_is_otg_enabled())) {
        furi_hal_power_disable_otg();
    }
    uhf->power_enabled = false;
    uhf->power_owned = false;
}

static bool seader_uhf_apply_ops_profile(SeaderUhf* uhf) {
    uint8_t payload[SEADER_UHF_RX_MAX] = {0};
    size_t payload_len = 0U;
    const uint8_t region = SEADER_UHF_REGION_US;
    const uint8_t channel = SEADER_UHF_CHANNEL_US_0;
    if(!seader_uhf_send_expect(
           uhf,
           seader_uhf_cmd_rf_off,
           sizeof(seader_uhf_cmd_rf_off),
           0x01U,
           0xB0U,
           payload,
           sizeof(payload),
           &payload_len) ||
       payload_len < 1U || payload[0] != 0x00U) {
        UHF_DIAG_W("apply_ops_profile: rf_off failed payload_len=%u", (unsigned)payload_len);
        return false;
    }
    furi_delay_ms(50);
    if(!seader_uhf_send_expect(
           uhf,
           seader_uhf_cmd_rf_on,
           sizeof(seader_uhf_cmd_rf_on),
           0x01U,
           0xB0U,
           payload,
           sizeof(payload),
           &payload_len) ||
       payload_len < 1U || payload[0] != 0x00U) {
        UHF_DIAG_W("apply_ops_profile: rf_on failed payload_len=%u", (unsigned)payload_len);
        return false;
    }
    furi_delay_ms(50);

    if(!seader_uhf_send_simple_ack(uhf, 0x07U, &region, 1U)) {
        UHF_DIAG_W("apply_ops_profile: set region failed");
        return false;
    }

    if(!seader_uhf_send_simple_ack(uhf, 0xABU, &channel, 1U)) {
        UHF_DIAG_W("apply_ops_profile: set channel failed");
        return false;
    }

    uint8_t query_payload[2] = {
        (uint8_t)(uhf->profile.query_word >> 8U), (uint8_t)uhf->profile.query_word};
    if(!seader_uhf_send_custom_expect(
           uhf,
           0x0EU,
           query_payload,
           sizeof(query_payload),
           0x01U,
           0x0EU,
           payload,
           sizeof(payload),
           &payload_len) ||
       payload_len < 1U || payload[0] != 0x00U) {
        UHF_DIAG_W("apply_ops_profile: set query failed payload_len=%u", (unsigned)payload_len);
        return false;
    }

    if(!seader_uhf_send_custom_expect(
           uhf,
           0x12U,
           &uhf->profile.inventory_mode,
           1U,
           0x01U,
           0x12U,
           payload,
           sizeof(payload),
           &payload_len) ||
       payload_len < 1U || payload[0] != 0x00U) {
        UHF_DIAG_W(
            "apply_ops_profile: inventory mode failed payload_len=%u", (unsigned)payload_len);
        return false;
    }
    return true;
}

static bool seader_uhf_select_epc(SeaderUhf* uhf, const uint8_t* epc, size_t epc_len) {
    uint8_t command_payload[40] = {0};
    uint8_t payload[SEADER_UHF_RX_MAX] = {0};
    size_t payload_len = 0U;
    if(!uhf || !epc || epc_len == 0U || epc_len > 31U) return false;

    command_payload[0] = 0x01U;
    command_payload[1] = 0x00U;
    command_payload[2] = 0x00U;
    command_payload[3] = 0x00U;
    command_payload[4] = 0x20U;
    command_payload[5] = (uint8_t)(epc_len * 8U);
    command_payload[6] = 0x00U;
    memcpy(command_payload + 7U, epc, epc_len);

    if(!seader_uhf_send_custom_expect(
           uhf,
           0x0CU,
           command_payload,
           7U + epc_len,
           0x01U,
           0x0CU,
           payload,
           sizeof(payload),
           &payload_len)) {
        return false;
    }
    return payload_len >= 1U && payload[0] == 0x00U;
}

static bool seader_uhf_select_tid(SeaderUhf* uhf, const uint8_t* tid, size_t tid_len) {
    uint8_t command_payload[40] = {0};
    uint8_t payload[SEADER_UHF_RX_MAX] = {0};
    size_t payload_len = 0U;
    if(!uhf || !tid || tid_len == 0U || tid_len > 31U) return false;

    command_payload[0] = 0x01U;
    command_payload[1] = 0x02U;
    command_payload[2] = 0x00U;
    command_payload[3] = 0x00U;
    command_payload[4] = 0x00U;
    command_payload[5] = (uint8_t)(tid_len * 8U);
    command_payload[6] = 0x00U;
    memcpy(command_payload + 7U, tid, tid_len);

    if(!seader_uhf_send_custom_expect(
           uhf,
           0x0CU,
           command_payload,
           7U + tid_len,
           0x01U,
           0x0CU,
           payload,
           sizeof(payload),
           &payload_len)) {
        return false;
    }
    return payload_len >= 1U && payload[0] == 0x00U;
}

static bool seader_uhf_read_memory(
    SeaderUhf* uhf,
    uint32_t access_password,
    uint8_t mem_bank,
    uint16_t word_ptr,
    uint16_t word_count,
    uint8_t* data_out,
    size_t data_out_cap,
    size_t* data_out_len) {
    uint8_t command_payload[9] = {0};
    uint8_t payload[SEADER_UHF_RX_MAX] = {0};
    size_t payload_len = 0U;
    if(!uhf || !data_out || !data_out_len) return false;

    command_payload[0] = (uint8_t)(access_password >> 24U);
    command_payload[1] = (uint8_t)(access_password >> 16U);
    command_payload[2] = (uint8_t)(access_password >> 8U);
    command_payload[3] = (uint8_t)access_password;
    command_payload[4] = (uint8_t)(mem_bank & 0x03U);
    command_payload[5] = (uint8_t)(word_ptr >> 8U);
    command_payload[6] = (uint8_t)word_ptr;
    command_payload[7] = (uint8_t)(word_count >> 8U);
    command_payload[8] = (uint8_t)word_count;

    for(uint8_t attempt = 0U; attempt < 4U; attempt++) {
        if(seader_uhf_send_custom_expect(
               uhf,
               0x39U,
               command_payload,
               sizeof(command_payload),
               0x01U,
               0x39U,
               payload,
               sizeof(payload),
               &payload_len) &&
           payload_len >= 2U) {
            uint8_t ul = payload[0];
            if((size_t)ul + 1U <= payload_len) {
                size_t data_len = payload_len - (size_t)ul - 1U;
                if(data_len > 0U && data_len <= data_out_cap) {
                    memcpy(data_out, payload + 1U + ul, data_len);
                    *data_out_len = data_len;
                    return true;
                }
            }
        }
        furi_delay_ms(15);
    }

    return false;
}

static bool seader_uhf_qt_write(SeaderUhf* uhf, uint32_t access_password, uint16_t control_word) {
    uint8_t command_payload[8] = {0};
    uint8_t payload[SEADER_UHF_RX_MAX] = {0};
    size_t payload_len = 0U;

    if(!uhf) return false;

    command_payload[0] = (uint8_t)(access_password >> 24U);
    command_payload[1] = (uint8_t)(access_password >> 16U);
    command_payload[2] = (uint8_t)(access_password >> 8U);
    command_payload[3] = (uint8_t)access_password;
    command_payload[4] = 0x01U;
    command_payload[5] = 0x01U;
    command_payload[6] = (uint8_t)(control_word >> 8U);
    command_payload[7] = (uint8_t)control_word;

    if(!seader_uhf_send_custom_expect(
           uhf,
           0xE5U,
           command_payload,
           sizeof(command_payload),
           0x01U,
           0xE6U,
           payload,
           sizeof(payload),
           &payload_len)) {
        return false;
    }

    // QT write success is reported by the E6 notification itself. Unlike simple
    // command ACKs, the payload is a tag notification and does not start with a
    // 0x00 status byte.
    return payload_len > 0U;
}

static bool seader_uhf_single_poll_once(
    SeaderUhf* uhf,
    uint8_t* epc_out,
    size_t epc_out_cap,
    size_t* epc_out_len) {
    uint8_t payload[SEADER_UHF_RX_MAX] = {0};
    size_t payload_len = 0U;

    if(!uhf || !epc_out || !epc_out_len) {
        return false;
    }

    return seader_uhf_send_expect(
               uhf,
               seader_uhf_cmd_single_poll,
               sizeof(seader_uhf_cmd_single_poll),
               0x02U,
               0x22U,
               payload,
               sizeof(payload),
               &payload_len) &&
           seader_uhf_parse_single_poll(payload, payload_len, epc_out, epc_out_cap, epc_out_len);
}

static bool seader_uhf_wait_for_selected_tag_response(
    SeaderUhf* uhf,
    uint32_t timeout_ms,
    uint8_t* epc_out,
    size_t epc_out_cap,
    size_t* epc_out_len) {
    uint32_t waited = 0U;
    uint8_t epc[SEADER_UHF_EPC_MAX_LEN] = {0};
    size_t epc_len = 0U;

    if(!uhf) {
        return false;
    }

    while(waited <= timeout_ms) {
        if(seader_uhf_single_poll_once(uhf, epc, sizeof(epc), &epc_len)) {
            if(epc_out && epc_out_len && epc_len <= epc_out_cap) {
                memcpy(epc_out, epc, epc_len);
                *epc_out_len = epc_len;
            }
            return true;
        }
        furi_delay_ms(SEADER_UHF_PRIVATE_WAIT_STEP_MS);
        waited += SEADER_UHF_PRIVATE_WAIT_STEP_MS;
    }

    return false;
}

static uint32_t seader_uhf_remaining_budget_ms(uint32_t started_at, uint32_t total_budget_ms) {
    const uint32_t elapsed = furi_get_tick() - started_at;
    return elapsed >= total_budget_ms ? 0U : (total_budget_ms - elapsed);
}

static bool seader_uhf_try_relock_public_tid(
    SeaderUhf* uhf,
    uint32_t access_password,
    const uint8_t* private_tid,
    size_t private_tid_len) {
    if(!uhf || !private_tid || private_tid_len == 0U || access_password == 0U) {
        return false;
    }

    for(uint8_t pass = 0U; pass < 2U; pass++) {
        if(seader_uhf_select_tid(uhf, private_tid, private_tid_len)) {
            furi_delay_ms(20);
            if(seader_uhf_qt_write(uhf, access_password, seader_uhf_qt_public_mask)) {
                return true;
            }
        }

        if(pass == 0U) {
            seader_uhf_close_session(uhf, false);
            furi_delay_ms(20);
            if(!seader_uhf_open_session(uhf) || !seader_uhf_apply_ops_profile(uhf)) {
                return false;
            }
        }
    }

    return false;
}

static bool seader_uhf_parse_single_poll(
    const uint8_t* payload,
    size_t payload_len,
    uint8_t* epc,
    size_t epc_cap,
    size_t* epc_len) {
    if(!payload || payload_len < 5U || !epc || !epc_len) return false;
    size_t parsed_epc_len = payload_len - 5U;
    if(parsed_epc_len == 0U || parsed_epc_len > epc_cap) return false;
    memcpy(epc, payload + 3U, parsed_epc_len);
    *epc_len = parsed_epc_len;
    return true;
}

SeaderUhf* seader_uhf_alloc(void) {
    SeaderUhf* uhf = calloc(1, sizeof(SeaderUhf));
    if(!uhf) return NULL;
    uhf->profile.baudrate = SEADER_UHF_UART_BAUDRATE;
    uhf->profile.query_word = 0x1020U;
    uhf->profile.inventory_mode = 0x00U;
    uhf->profile.family = SeaderUhfModuleFamilyM100;
    uhf->profile.hardware_class = SeaderUhfHardwareClass26dBm;
    return uhf;
}

void seader_uhf_free(SeaderUhf* uhf) {
    if(!uhf) return;
    seader_uhf_end(uhf);
    free(uhf);
}

bool seader_uhf_begin(SeaderUhf* uhf) {
    if(!uhf) return false;

    seader_uhf_reset_runtime_state(uhf);
    return seader_uhf_refresh_presence(uhf);
}

void seader_uhf_end(SeaderUhf* uhf) {
    if(!uhf) return;
    seader_uhf_power_off(uhf);
    seader_uhf_reset_runtime_state(uhf);
}

bool seader_uhf_refresh_presence(SeaderUhf* uhf) {
    uint8_t hw_payload[SEADER_UHF_RX_MAX] = {0};
    size_t hw_payload_len = 0U;
    bool temporary_power = false;
    bool detected = false;
    if(!uhf) return false;
    uhf->presence = SeaderUhfModulePresenceAbsent;

    if(!uhf->power_enabled) {
        if(!seader_uhf_power_on(uhf)) return false;
        temporary_power = true;
    }

    for(uint8_t attempt = 0U; attempt < SEADER_UHF_DETECT_ATTEMPTS; attempt++) {
        if(!seader_uhf_open_session(uhf)) {
            furi_delay_ms(SEADER_UHF_DETECT_RETRY_MS);
            continue;
        }

        detected = seader_uhf_send_expect(
            uhf,
            seader_uhf_cmd_hw_version,
            sizeof(seader_uhf_cmd_hw_version),
            0x01U,
            0x03U,
            hw_payload,
            sizeof(hw_payload),
            &hw_payload_len);
        seader_uhf_close_session(uhf, false);

        if(detected) break;
        furi_delay_ms(SEADER_UHF_DETECT_RETRY_MS);
    }

    if(!detected) {
        if(seader_uhf_should_power_off_after_probe(temporary_power, detected)) {
            seader_uhf_power_off(uhf);
        }
        return false;
    }

    uhf->presence = SeaderUhfModulePresencePresent;
    return true;
}

bool seader_uhf_is_available(const SeaderUhf* uhf) {
    return uhf && uhf->presence == SeaderUhfModulePresencePresent;
}

const SeaderUhfModuleProfile* seader_uhf_get_profile(const SeaderUhf* uhf) {
    return uhf ? &uhf->profile : NULL;
}

const SeaderUhfTagSnapshot* seader_uhf_get_snapshot(const SeaderUhf* uhf) {
    return uhf ? &uhf->snapshot : NULL;
}

bool seader_uhf_inventory_tag(SeaderUhf* uhf, SeaderUhfReadResult* result) {
    uint8_t payload[SEADER_UHF_RX_MAX] = {0};
    uint8_t tid_bank[SEADER_UHF_TID_MAX_LEN] = {0};
    SeaderUhfTagSnapshot candidate = {0};
    size_t payload_len = 0U;
    size_t tid_bank_len = 0U;
    bool tag_found = false;
    bool select_ok = false;
    bool tid_read_ok = false;
    bool csn_derived_ok = false;
    bool public_epc_flow = false;
    if(!uhf || !result) return false;
    memset(result, 0, sizeof(*result));
    result->failure_reason = PluginUhfReadFailureReasonInternalState;
    if(!seader_uhf_power_on(uhf) || !seader_uhf_open_session(uhf)) {
        UHF_DIAG_W("inventory: power/session open failed");
        result->failure_reason = PluginUhfReadFailureReasonModuleUnavailable;
        return false;
    }
    do {
        if(!seader_uhf_apply_ops_profile(uhf)) {
            UHF_DIAG_W("inventory: apply ops profile failed");
            result->failure_reason = PluginUhfReadFailureReasonModuleUnavailable;
            break;
        }

        for(uint8_t i = 0U; i < 6U; i++) {
            if(seader_uhf_send_expect(
                   uhf,
                   seader_uhf_cmd_single_poll,
                   sizeof(seader_uhf_cmd_single_poll),
                   0x02U,
                   0x22U,
                   payload,
                   sizeof(payload),
                   &payload_len) &&
               seader_uhf_parse_single_poll(
                   payload, payload_len, candidate.epc, sizeof(candidate.epc), &candidate.epc_len)) {
                tag_found = true;
                break;
            }
            furi_delay_ms(50);
        }

        if(!tag_found) {
            UHF_DIAG_W("inventory: no tag response");
            result->failure_reason = PluginUhfReadFailureReasonNoTagSeen;
            break;
        }
        select_ok = seader_uhf_select_epc(uhf, candidate.epc, candidate.epc_len);
        if(!select_ok) {
            UHF_DIAG_W("inventory: select EPC failed");
            result->failure_reason = PluginUhfReadFailureReasonSelectFailed;
            break;
        }
        furi_delay_ms(20);
        public_epc_flow = seader_uhf_is_public_epc_form(candidate.epc, candidate.epc_len);
        if(public_epc_flow) {
            csn_derived_ok = seader_uhf_build_sam_csn_from_form(
                candidate.epc,
                candidate.epc_len,
                candidate.sam_csn,
                sizeof(candidate.sam_csn),
                &candidate.sam_csn_len);
            tid_read_ok = seader_uhf_read_memory(
                uhf,
                0U,
                0x02U,
                0x0000U,
                SEADER_UHF_WORDS_TID_READ,
                tid_bank,
                sizeof(tid_bank),
                &tid_bank_len);
            if(tid_read_ok) {
                tid_read_ok = seader_uhf_extract_trace_tid_from_tid_bank(
                    tid_bank,
                    tid_bank_len,
                    candidate.public_tid,
                    sizeof(candidate.public_tid),
                    &candidate.public_tid_len);
            }
            if(!tid_read_ok) {
                if(csn_derived_ok) {
                    ;
                } else {
                    UHF_DIAG_W("inventory: public TID read failed");
                    result->failure_reason = PluginUhfReadFailureReasonPublicTidReadFailed;
                    break;
                }
            }
            if(!csn_derived_ok && tid_read_ok) {
                csn_derived_ok = seader_uhf_build_sam_csn_from_form(
                    candidate.public_tid,
                    candidate.public_tid_len,
                    candidate.sam_csn,
                    sizeof(candidate.sam_csn),
                    &candidate.sam_csn_len);
            }
        } else {
            memcpy(candidate.private_epc, candidate.epc, candidate.epc_len);
            candidate.private_epc_len = candidate.epc_len;
            tid_read_ok = seader_uhf_read_memory(
                uhf,
                0U,
                0x02U,
                0x0000U,
                SEADER_UHF_WORDS_TID_READ,
                tid_bank,
                sizeof(tid_bank),
                &tid_bank_len);
            if(!tid_read_ok || !seader_uhf_extract_trace_tid_from_tid_bank(
                                   tid_bank,
                                   tid_bank_len,
                                   candidate.private_tid,
                                   sizeof(candidate.private_tid),
                                   &candidate.private_tid_len)) {
                UHF_DIAG_W("inventory: private TID read failed");
                result->failure_reason = PluginUhfReadFailureReasonPublicTidReadFailed;
                break;
            }
            csn_derived_ok = seader_uhf_build_sam_csn_from_form(
                candidate.private_tid,
                candidate.private_tid_len,
                candidate.sam_csn,
                sizeof(candidate.sam_csn),
                &candidate.sam_csn_len);
        }
        if(!csn_derived_ok) {
            UHF_DIAG_W(
                "inventory: SAM CSN derive failed epc_len=%u tid_len=%u",
                (unsigned)candidate.epc_len,
                (unsigned)(public_epc_flow ? candidate.public_tid_len :
                                             candidate.private_tid_len));
            result->failure_reason = PluginUhfReadFailureReasonUnsupportedShape;
            break;
        }
    } while(false);
    seader_uhf_close_session(uhf, false);

    const bool ok = seader_uhf_inventory_identification_complete(
        tag_found, select_ok, tid_read_ok, csn_derived_ok);
    if(ok) {
        memcpy(&uhf->snapshot, &candidate, sizeof(uhf->snapshot));
        memcpy(&result->tag, &candidate, sizeof(result->tag));
        result->module_ok = true;
        result->failure_reason = PluginUhfReadFailureReasonNone;
    }
    return ok;
}

bool seader_uhf_set_access_password(SeaderUhf* uhf, const uint8_t* key, size_t key_len) {
    if(!uhf || !key || key_len != 4U) return false;
    memcpy(uhf->snapshot.access_password, key, 4U);
    uhf->snapshot.access_password_len = 4U;
    return true;
}

bool seader_uhf_read_private_data(SeaderUhf* uhf, SeaderUhfReadResult* result) {
    uint8_t tid[SEADER_UHF_TID_MAX_LEN] = {0};
    uint8_t user[SEADER_UHF_USER_DATA_MAX_LEN] = {0};
    uint8_t observed_epc[SEADER_UHF_EPC_MAX_LEN] = {0};
    size_t tid_len = 0U;
    size_t user_len = 0U;
    size_t observed_epc_len = 0U;
    uint32_t access_password = 0U;
    uint32_t wait_budget_ms = 0U;
    bool relock_needed = false;
    bool public_epc_flow = false;
    bool private_start_flow = false;
    const uint32_t started_at = furi_get_tick();
    if(!uhf || !result || uhf->snapshot.epc_len == 0U) return false;
    memset(result, 0, sizeof(*result));
    result->failure_reason = PluginUhfReadFailureReasonInternalState;
    if(uhf->snapshot.access_password_len == 4U) {
        access_password = ((uint32_t)uhf->snapshot.access_password[0] << 24U) |
                          ((uint32_t)uhf->snapshot.access_password[1] << 16U) |
                          ((uint32_t)uhf->snapshot.access_password[2] << 8U) |
                          uhf->snapshot.access_password[3];
    }

    if(!seader_uhf_power_on(uhf) || !seader_uhf_open_session(uhf)) return false;
    bool ok = false;
    do {
#define UHF_PD_REQUIRE_BUDGET(reason_value)                                      \
        do {                                                                     \
            if(seader_uhf_remaining_budget_ms(started_at, SEADER_UHF_PRIVATE_TOTAL_MS) == 0U) { \
                result->failure_reason = (reason_value);                         \
                break;                                                           \
            }                                                                    \
        } while(0)

        UHF_PD_REQUIRE_BUDGET(PluginUhfReadFailureReasonPrivateDataReadFailed);
        public_epc_flow = seader_uhf_is_public_epc_form(uhf->snapshot.epc, uhf->snapshot.epc_len);
        private_start_flow = !public_epc_flow &&
                             uhf->snapshot.private_tid_len == SEADER_UHF_TRACE_TID_LEN;
        UHF_PD_STAGE(result, PluginUhfDebugStagePdApplyProfile, "apply profile");
        if(!seader_uhf_apply_ops_profile(uhf)) {
            result->failure_reason = PluginUhfReadFailureReasonModuleUnavailable;
            break;
        }

        if(public_epc_flow) {
            if(access_password == 0U) {
                result->failure_reason = PluginUhfReadFailureReasonUnlockFailed;
                UHF_DIAG_W("private_data: public flow missing access password");
                break;
            }

            UHF_PD_STAGE(result, PluginUhfDebugStagePdPublicSelectEpc, "select public EPC");
            if(!seader_uhf_select_epc(uhf, uhf->snapshot.epc, uhf->snapshot.epc_len)) {
                result->failure_reason = PluginUhfReadFailureReasonSelectFailed;
                UHF_DIAG_W("private_data: select EPC failed");
                break;
            }
            furi_delay_ms(20);

            UHF_PD_REQUIRE_BUDGET(PluginUhfReadFailureReasonUnlockFailed);
            UHF_PD_STAGE(result, PluginUhfDebugStagePdPublicUnlockQt, "qt private");
            if(!seader_uhf_qt_write(uhf, access_password, 0x0000U)) {
                UHF_DIAG_W("private_data: unlock QT failed");
                result->failure_reason = PluginUhfReadFailureReasonUnlockFailed;
                break;
            }
            UHF_PD_STAGE(result, PluginUhfDebugStagePdPublicDeriveTid, "derive private TID");
            if(!seader_uhf_build_private_tid_from_form(
                   uhf->snapshot.epc, uhf->snapshot.epc_len, tid, sizeof(tid), &tid_len) &&
               !seader_uhf_build_private_tid_from_form(
                   uhf->snapshot.public_tid,
                   uhf->snapshot.public_tid_len,
                   tid,
                   sizeof(tid),
                   &tid_len)) {
                result->failure_reason = PluginUhfReadFailureReasonPrivateDataReadFailed;
                UHF_DIAG_W("private_data: unable to derive private TID");
                break;
            }

            seader_uhf_close_session(uhf, false);
            furi_delay_ms(20);
            UHF_PD_REQUIRE_BUDGET(PluginUhfReadFailureReasonPrivateDataReadFailed);
            UHF_PD_STAGE(
                result, PluginUhfDebugStagePdPublicRefreshAfterUnlock, "refresh after unlock");
            if(!seader_uhf_open_session(uhf) || !seader_uhf_apply_ops_profile(uhf)) {
                result->failure_reason = PluginUhfReadFailureReasonPrivateDataReadFailed;
                UHF_DIAG_W("private_data: refresh after unlock failed");
                break;
            }

            UHF_PD_STAGE(result, PluginUhfDebugStagePdPublicSelectTid, "select private TID");
            if(!seader_uhf_select_tid(uhf, tid, tid_len)) {
                result->failure_reason = PluginUhfReadFailureReasonPrivateDataReadFailed;
                UHF_DIAG_W("private_data: select private TID failed");
                break;
            }
            furi_delay_ms(20);

            UHF_PD_STAGE(result, PluginUhfDebugStagePdPublicWaitSelected, "wait selected by TID");
            wait_budget_ms = seader_uhf_remaining_budget_ms(started_at, SEADER_UHF_PRIVATE_TOTAL_MS);
            if(wait_budget_ms > SEADER_UHF_PRIVATE_WAIT_MS) {
                wait_budget_ms = SEADER_UHF_PRIVATE_WAIT_MS;
            }
            if(!seader_uhf_wait_for_selected_tag_response(
                   uhf,
                   wait_budget_ms,
                   observed_epc,
                   sizeof(observed_epc),
                   &observed_epc_len)) {
                result->failure_reason = PluginUhfReadFailureReasonPrivateDataReadFailed;
                UHF_DIAG_W("private_data: private TID not observed after unlock");
                break;
            }

            if(observed_epc_len > 0U) {
                if(!seader_uhf_is_public_epc_form(observed_epc, observed_epc_len)) {
                    memcpy(uhf->snapshot.private_epc, observed_epc, observed_epc_len);
                    uhf->snapshot.private_epc_len = observed_epc_len;
                }
            }

            UHF_PD_STAGE(result, PluginUhfDebugStagePdPublicReadUser, "read USER");
            if(!seader_uhf_read_memory(
                   uhf,
                   0U,
                   0x03U,
                   0x0000U,
                   SEADER_UHF_WORDS_USER_READ,
                   user,
                   sizeof(user),
                   &user_len)) {
                result->failure_reason = PluginUhfReadFailureReasonPrivateDataReadFailed;
                UHF_DIAG_W("private_data: read user bank failed");
                break;
            }
            memcpy(uhf->snapshot.private_tid, tid, tid_len);
            uhf->snapshot.private_tid_len = tid_len;
            memcpy(uhf->snapshot.user_data, user, user_len);
            uhf->snapshot.user_data_len = user_len;
            relock_needed = true;
            ok = true;
            goto private_read_done;
        }

        if(!private_start_flow) {
            result->failure_reason = PluginUhfReadFailureReasonUnsupportedShape;
            UHF_DIAG_W("private_data: unsupported private-start state");
            break;
        }

        if(access_password == 0U) {
            result->failure_reason = PluginUhfReadFailureReasonPrivateDataReadFailed;
            UHF_DIAG_W("private_data: private-start flow missing access password");
            break;
        }

        if(uhf->snapshot.private_tid_len == SEADER_UHF_TRACE_TID_LEN) {
            memcpy(tid, uhf->snapshot.private_tid, uhf->snapshot.private_tid_len);
            tid_len = uhf->snapshot.private_tid_len;
        } else {
            UHF_PD_STAGE(result, PluginUhfDebugStagePdPrivateReadTid, "read TID");
            if(!seader_uhf_read_memory(
                   uhf, 0U, 0x02U, 0x0000U, SEADER_UHF_WORDS_TID_READ, tid, sizeof(tid), &tid_len) &&
               !seader_uhf_read_memory(
                   uhf,
                   access_password,
                   0x02U,
                   0x0000U,
                   SEADER_UHF_WORDS_TID_READ,
                   tid,
                   sizeof(tid),
                   &tid_len)) {
                result->failure_reason = PluginUhfReadFailureReasonPrivateDataReadFailed;
                UHF_DIAG_W("private_data: read TID bank failed");
                break;
            }
            UHF_PD_STAGE(result, PluginUhfDebugStagePdPrivateParseTid, "parse TID");
            if(!seader_uhf_extract_trace_tid_from_tid_bank(
                   tid, tid_len, tid, sizeof(tid), &tid_len)) {
                result->failure_reason = PluginUhfReadFailureReasonPrivateDataReadFailed;
                UHF_DIAG_W("private_data: invalid private TID bank");
                break;
            }
        }
        UHF_PD_STAGE(result, PluginUhfDebugStagePdPrivateSelectTid, "select private TID");
        if(!seader_uhf_select_tid(uhf, tid, tid_len)) {
            result->failure_reason = PluginUhfReadFailureReasonPrivateDataReadFailed;
            UHF_DIAG_W("private_data: select private TID failed");
            break;
        }
        furi_delay_ms(20);

        UHF_PD_REQUIRE_BUDGET(PluginUhfReadFailureReasonPrivateDataReadFailed);
        wait_budget_ms = seader_uhf_remaining_budget_ms(started_at, SEADER_UHF_PRIVATE_TOTAL_MS);
        if(wait_budget_ms > 400U) {
            wait_budget_ms = 400U;
        }
        (void)seader_uhf_wait_for_selected_tag_response(
            uhf, wait_budget_ms, observed_epc, sizeof(observed_epc), &observed_epc_len);
        if(observed_epc_len > 0U &&
           !seader_uhf_is_public_epc_form(observed_epc, observed_epc_len)) {
            memcpy(uhf->snapshot.private_epc, observed_epc, observed_epc_len);
            uhf->snapshot.private_epc_len = observed_epc_len;
        }

        UHF_PD_STAGE(result, PluginUhfDebugStagePdPrivateReadUser, "read USER");
        if(!seader_uhf_read_memory(
               uhf, 0U, 0x03U, 0x0000U, SEADER_UHF_WORDS_USER_READ, user, sizeof(user), &user_len) &&
           !seader_uhf_read_memory(
               uhf,
               access_password,
               0x03U,
               0x0000U,
               SEADER_UHF_WORDS_USER_READ,
               user,
               sizeof(user),
               &user_len)) {
            result->failure_reason = PluginUhfReadFailureReasonPrivateDataReadFailed;
            UHF_DIAG_W("private_data: read user bank failed");
            break;
        }
        memcpy(uhf->snapshot.private_tid, tid, tid_len);
        uhf->snapshot.private_tid_len = tid_len;
        memcpy(uhf->snapshot.user_data, user, user_len);
        uhf->snapshot.user_data_len = user_len;
        relock_needed = true;
        ok = true;
    private_read_done:
        if(observed_epc_len > 0U &&
           !seader_uhf_is_public_epc_form(observed_epc, observed_epc_len)) {
            memcpy(uhf->snapshot.private_epc, observed_epc, observed_epc_len);
            uhf->snapshot.private_epc_len = observed_epc_len;
        }
    } while(false);

    if(relock_needed) {
        UHF_PD_STAGE(result, PluginUhfDebugStagePdRelock, "relock public");
        const bool relock_ok =
            ok && tid_len > 0U &&
            seader_uhf_try_relock_public_tid(uhf, access_password, tid, tid_len);
        if(!relock_ok) {
            result->failure_reason = PluginUhfReadFailureReasonRelockFailed;
            UHF_DIAG_W("private_data: relock failed");
        }
        ok = relock_ok;
    }
    seader_uhf_close_session(uhf, false);
    if(!ok) {
        seader_uhf_power_off(uhf);
    }

    if(ok) {
        result->module_ok = true;
        result->private_data_ok = true;
        result->failure_reason = PluginUhfReadFailureReasonNone;
        memcpy(&result->tag, &uhf->snapshot, sizeof(result->tag));
    } else {
        UHF_DIAG_W("private_data: failed");
    }
#undef UHF_PD_REQUIRE_BUDGET
    return ok;
}

void seader_uhf_hibernate(SeaderUhf* uhf) {
    seader_uhf_power_off(uhf);
}

bool seader_uhf_build_i2c_response(
    SeaderUhf* uhf,
    const PluginUhfRequest* request,
    PluginUhfResponse* response) {
    if(!uhf || !request || !response) {
        return false;
    }
    if(request->bus_address != SEADER_UHF_BUS_ADDRESS) return false;
    memset(response, 0, sizeof(*response));

    switch(request->command) {
    case PluginUhfCommandGetVersion:
        response->type = PluginUhfResponseVersion;
        response->version_len = sizeof(seader_uhf_version_raw) - 1U;
        memcpy(response->version, seader_uhf_version_raw, response->version_len);
        break;
    case PluginUhfCommandGetProperties:
        response->type = PluginUhfResponseGetProperties;
        break;
    case PluginUhfCommandSetAccessPassword:
        if(!seader_uhf_should_ack_access_password(request->command_value_len) ||
           !seader_uhf_set_access_password(
               uhf, request->command_value, request->command_value_len)) {
            response->failure_reason = PluginUhfReadFailureReasonInternalState;
            return false;
        }
        response->type = PluginUhfResponseSetAccessPassword;
        break;
    case PluginUhfCommandGetPrivateData: {
        SeaderUhfReadResult result = {0};
        uint8_t sam_tid[SEADER_UHF_TRACE_TID_LEN] = {0};
        uint8_t sam_user_data[SEADER_UHF_SAM_USER_DATA_LEN] = {0};
        size_t sam_tid_len = 0U;
        size_t sam_user_data_len = 0U;
        if(!seader_uhf_read_private_data(uhf, &result)) {
            response->failure_reason = result.failure_reason;
            response->debug_stage = result.debug_stage;
            UHF_DIAG_W("i2c getPrivateData failed");
            return false;
        }
        response->debug_stage = result.debug_stage;
        UHF_PD_STAGE(&result, PluginUhfDebugStagePdNormalize, "normalize for SAM");
        if(!seader_uhf_normalize_private_data_for_sam(
               result.tag.private_tid,
               result.tag.private_tid_len,
               result.tag.user_data,
               result.tag.user_data_len,
               sam_tid,
               sizeof(sam_tid),
               &sam_tid_len,
               sam_user_data,
               sizeof(sam_user_data),
               &sam_user_data_len)) {
            response->failure_reason = PluginUhfReadFailureReasonInternalState;
            response->debug_stage = PluginUhfDebugStagePdNormalize;
            return false;
        }
        response->type = PluginUhfResponseGetPrivateData;
        response->tid_len = sam_tid_len;
        response->user_data_len = sam_user_data_len;
        memcpy(response->tid, sam_tid, sam_tid_len);
        memcpy(response->user_data, sam_user_data, sam_user_data_len);
        break;
    }
    default:
        return false;
    }
    return true;
}
