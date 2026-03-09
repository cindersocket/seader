#include "uhf_module.h"

#include "seader_i.h"
#include "uhf_frame.h"
#include "uhf_logic.h"
#include "uhf_uart.h"

#include <I2CCommand.h>
#include <I2CClockSpeed.h>
#include <Response.h>
#include <UHFModuleResponse.h>

#define TAG                             "SeaderUHF"
#define SEADER_UHF_UART_ID              FuriHalSerialIdUsart
#define SEADER_UHF_UART_BAUDRATE        115200U
#define SEADER_UHF_RX_MAX               96U
#define SEADER_UHF_PROBE_TIMEOUT_MS     220U
#define SEADER_UHF_PROBE_STEP_MS        5U
#define SEADER_UHF_POWER_SETTLE_MS      120U
#define SEADER_UHF_CLOSE_SETTLE_MS      20U
#define SEADER_UHF_BUS_ADDRESS          0x2EU
#define SEADER_UHF_WORDS_TID_READ       0x000CU
#define SEADER_UHF_WORDS_USER_READ      0x001FU

struct SeaderUhf {
    SeaderUhfUart uart;
    SeaderUhfModulePresence presence;
    SeaderUhfModuleProfile profile;
    uint8_t version_raw[SEADER_UHF_MODULE_VERSION_MAX_LEN];
    size_t version_raw_len;
    char hw_version[24];
    char sw_version[24];
    SeaderUhfTagSnapshot snapshot;
    bool power_enabled;
    bool power_owned;
};

static const uint8_t seader_uhf_cmd_hw_version[] = {0xBB, 0x00, 0x03, 0x00, 0x01, 0x00, 0x04, 0x7E};
static const uint8_t seader_uhf_cmd_sw_version[] = {0xBB, 0x00, 0x03, 0x00, 0x01, 0x01, 0x05, 0x7E};
static const uint8_t seader_uhf_cmd_single_poll[] = {0xBB, 0x00, 0x22, 0x00, 0x00, 0x22, 0x7E};
static const uint8_t seader_uhf_cmd_hibernate[] = {0xBB, 0x00, 0x17, 0x00, 0x00, 0x17, 0x7E};
static const uint8_t seader_uhf_cmd_rf_off[] = {0xBB, 0x00, 0xB0, 0x00, 0x01, 0x00, 0xB1, 0x7E};
static const uint8_t seader_uhf_cmd_rf_on[] = {0xBB, 0x00, 0xB0, 0x00, 0x01, 0xFF, 0xB0, 0x7E};
static const uint16_t seader_uhf_qt_public_mask = 0x4000U;

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

static size_t seader_uhf_copy_ascii(char* dst, size_t dst_cap, const uint8_t* src, size_t src_len) {
    if(!dst || dst_cap == 0U || !src) return 0U;
    size_t out = 0U;
    for(size_t i = 0U; i < src_len && out + 1U < dst_cap; i++) {
        if(src[i] >= 0x20U && src[i] <= 0x7EU) dst[out++] = (char)src[i];
    }
    dst[out] = '\0';
    return out;
}

static void seader_uhf_apply_profile_from_versions(SeaderUhf* uhf) {
    uhf->profile.family = SeaderUhfModuleFamilyUnknown;
    uhf->profile.hardware_class = SeaderUhfHardwareClassUnknown;
    uhf->profile.baudrate = SEADER_UHF_UART_BAUDRATE;
    uhf->profile.query_word = 0x1020U;
    uhf->profile.inventory_mode = 0x00U;

    if(strstr(uhf->hw_version, "QM100")) {
        uhf->profile.family = SeaderUhfModuleFamilyQM100;
    } else if(strstr(uhf->hw_version, "M100")) {
        uhf->profile.family = SeaderUhfModuleFamilyM100;
    }

    if(strstr(uhf->hw_version, "30dBm")) {
        uhf->profile.hardware_class = SeaderUhfHardwareClass30dBm;
    } else if(strstr(uhf->hw_version, "26dBm")) {
        uhf->profile.hardware_class = SeaderUhfHardwareClass26dBm;
    } else if(strstr(uhf->hw_version, "20dBm")) {
        uhf->profile.hardware_class = SeaderUhfHardwareClass20dBm;
    }
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
    if(uhf->power_enabled) return true;

    const bool already_on = furi_hal_power_is_otg_enabled();
    if(!already_on) {
        furi_hal_power_enable_otg();
        uhf->power_owned = true;
    } else {
        uhf->power_owned = false;
    }
    uhf->power_enabled = true;
    furi_delay_ms(SEADER_UHF_POWER_SETTLE_MS);
    return true;
}

static void seader_uhf_power_off(SeaderUhf* uhf) {
    if(!uhf) return;

    seader_uhf_close_session(uhf, true);
    if(uhf->power_enabled && uhf->power_owned && furi_hal_power_is_otg_enabled()) {
        furi_hal_power_disable_otg();
    }
    uhf->power_enabled = false;
    uhf->power_owned = false;
}

static bool seader_uhf_apply_ops_profile(SeaderUhf* uhf) {
    uint8_t payload[SEADER_UHF_RX_MAX] = {0};
    size_t payload_len = 0U;
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
        return false;
    }
    furi_delay_ms(50);

    uint8_t query_payload[2] = {
        (uint8_t)(uhf->profile.query_word >> 8U), (uint8_t)uhf->profile.query_word};
    if(!seader_uhf_send_custom_expect(
           uhf, 0x0EU, query_payload, sizeof(query_payload), 0x01U, 0x0EU, payload, sizeof(payload), &payload_len) ||
       payload_len < 1U || payload[0] != 0x00U) {
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

static bool seader_uhf_select_mask(
    SeaderUhf* uhf,
    uint8_t mem_bank,
    uint32_t bit_ptr,
    const uint8_t* mask,
    size_t mask_len) {
    uint8_t command_payload[48] = {0};
    uint8_t payload[SEADER_UHF_RX_MAX] = {0};
    size_t payload_len = 0U;

    if(!uhf || !mask || mask_len == 0U || mask_len > 31U) return false;

    command_payload[0] = (uint8_t)(mem_bank & 0x03U);
    command_payload[1] = (uint8_t)(bit_ptr >> 24U);
    command_payload[2] = (uint8_t)(bit_ptr >> 16U);
    command_payload[3] = (uint8_t)(bit_ptr >> 8U);
    command_payload[4] = (uint8_t)bit_ptr;
    command_payload[5] = (uint8_t)(mask_len * 8U);
    command_payload[6] = 0x00U;
    memcpy(command_payload + 7U, mask, mask_len);

    if(!seader_uhf_send_custom_expect(
           uhf,
           0x0CU,
           command_payload,
           7U + mask_len,
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

static bool seader_uhf_qt_read(SeaderUhf* uhf, uint32_t access_password, uint16_t* control_out) {
    uint8_t command_payload[8] = {0};
    uint8_t payload[SEADER_UHF_RX_MAX] = {0};
    size_t payload_len = 0U;

    if(!uhf || !control_out) return false;

    command_payload[0] = (uint8_t)(access_password >> 24U);
    command_payload[1] = (uint8_t)(access_password >> 16U);
    command_payload[2] = (uint8_t)(access_password >> 8U);
    command_payload[3] = (uint8_t)access_password;
    command_payload[4] = 0x00U;
    command_payload[5] = 0x01U;
    command_payload[6] = 0x00U;
    command_payload[7] = 0x00U;

    if(!seader_uhf_send_custom_expect(
           uhf, 0xE5U, command_payload, sizeof(command_payload), 0x01U, 0xE5U, payload, sizeof(payload), &payload_len)) {
        return false;
    }
    if(payload_len < 3U || payload[0] != 0x00U) return false;

    *control_out = (uint16_t)(((uint16_t)payload[1] << 8U) | payload[2]);
    return true;
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
           uhf, 0xE5U, command_payload, sizeof(command_payload), 0x01U, 0xE6U, payload, sizeof(payload), &payload_len)) {
        return false;
    }

    return payload_len >= 1U && payload[0] == 0x00U;
}

static bool seader_uhf_try_relock_public(SeaderUhf* uhf, uint32_t access_password, const uint8_t* tid) {
    uint16_t lock_control = seader_uhf_qt_public_mask;
    uint16_t current_control = 0U;

    if(!uhf || !tid) return false;
    if(!seader_uhf_select_mask(uhf, 0x02U, 0x00000000U, tid, SEADER_UHF_TRACE_TID_LEN)) return false;
    furi_delay_ms(20);

    if(seader_uhf_qt_read(uhf, access_password, &current_control)) {
        lock_control = (uint16_t)(current_control | seader_uhf_qt_public_mask);
    }

    return seader_uhf_qt_write(uhf, access_password, lock_control);
}

static bool seader_uhf_parse_single_poll(
    const uint8_t* payload, size_t payload_len, uint8_t* epc, size_t epc_cap, size_t* epc_len) {
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
    if(!seader_uhf_power_on(uhf)) return false;
    return seader_uhf_refresh_presence(uhf);
}

void seader_uhf_end(SeaderUhf* uhf) {
    if(!uhf) return;
    seader_uhf_power_off(uhf);
    seader_uhf_reset_runtime_state(uhf);
}

bool seader_uhf_refresh_presence(SeaderUhf* uhf) {
    uint8_t hw_payload[SEADER_UHF_RX_MAX] = {0};
    uint8_t sw_payload[SEADER_UHF_RX_MAX] = {0};
    size_t hw_payload_len = 0U;
    size_t sw_payload_len = 0U;
    bool temporary_power = false;
    if(!uhf) return false;

    memset(uhf->hw_version, 0, sizeof(uhf->hw_version));
    memset(uhf->sw_version, 0, sizeof(uhf->sw_version));
    memset(uhf->version_raw, 0, sizeof(uhf->version_raw));
    uhf->version_raw_len = 0U;
    uhf->presence = SeaderUhfModulePresenceAbsent;

    if(!uhf->power_enabled) {
        if(!seader_uhf_power_on(uhf)) return false;
        temporary_power = true;
    }

    if(!seader_uhf_open_session(uhf)) {
        if(temporary_power) {
            seader_uhf_power_off(uhf);
        }
        return false;
    }
    bool hw_ok = seader_uhf_send_expect(
        uhf,
        seader_uhf_cmd_hw_version,
        sizeof(seader_uhf_cmd_hw_version),
        0x01U,
        0x03U,
        hw_payload,
        sizeof(hw_payload),
        &hw_payload_len);
    furi_delay_ms(10);
    bool sw_ok = seader_uhf_send_expect(
        uhf,
        seader_uhf_cmd_sw_version,
        sizeof(seader_uhf_cmd_sw_version),
        0x01U,
        0x03U,
        sw_payload,
        sizeof(sw_payload),
        &sw_payload_len);
    seader_uhf_close_session(uhf, temporary_power);
    if(temporary_power) {
        if(uhf->power_owned && furi_hal_power_is_otg_enabled()) {
            furi_hal_power_disable_otg();
        }
        uhf->power_enabled = false;
        uhf->power_owned = false;
    }

    if(!hw_ok && !sw_ok) return false;

    seader_uhf_copy_ascii(uhf->hw_version, sizeof(uhf->hw_version), hw_payload, hw_payload_len);
    seader_uhf_copy_ascii(uhf->sw_version, sizeof(uhf->sw_version), sw_payload, sw_payload_len);
    uhf->version_raw_len = (size_t)snprintf(
        (char*)uhf->version_raw,
        sizeof(uhf->version_raw),
        "SW=%s|HW=%s",
        uhf->sw_version,
        uhf->hw_version);
    if(uhf->version_raw_len >= sizeof(uhf->version_raw)) {
        uhf->version_raw_len = sizeof(uhf->version_raw) - 1U;
    }

    seader_uhf_apply_profile_from_versions(uhf);
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

const char* seader_uhf_get_hw_version(const SeaderUhf* uhf) {
    return uhf ? uhf->hw_version : "";
}

const char* seader_uhf_get_sw_version(const SeaderUhf* uhf) {
    return uhf ? uhf->sw_version : "";
}

bool seader_uhf_inventory_tag(SeaderUhf* uhf, SeaderUhfReadResult* result) {
    uint8_t payload[SEADER_UHF_RX_MAX] = {0};
    size_t payload_len = 0U;
    if(!uhf || !result) return false;
    memset(result, 0, sizeof(*result));

    if(!seader_uhf_power_on(uhf) || !seader_uhf_open_session(uhf)) return false;
    bool ok = false;
    do {
        if(!seader_uhf_apply_ops_profile(uhf)) break;

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
                   payload,
                   payload_len,
                   result->tag.epc,
                   sizeof(result->tag.epc),
                   &result->tag.epc_len)) {
                ok = true;
                break;
            }
            furi_delay_ms(50);
        }

        if(!ok) break;
        if(!seader_uhf_select_epc(uhf, result->tag.epc, result->tag.epc_len)) break;
        furi_delay_ms(20);
        if(!seader_uhf_read_memory(
               uhf,
               0U,
               0x02U,
               0x0000U,
               SEADER_UHF_WORDS_TID_READ,
               result->tag.public_tid,
               sizeof(result->tag.public_tid),
               &result->tag.public_tid_len)) {
            break;
        }
        if(!seader_uhf_build_sam_csn_from_public_tid(
               result->tag.public_tid,
               result->tag.public_tid_len,
               result->tag.sam_csn,
               sizeof(result->tag.sam_csn),
               &result->tag.sam_csn_len)) {
            break;
        }
    } while(false);
    seader_uhf_close_session(uhf, false);

    if(ok) {
        memcpy(&uhf->snapshot, &result->tag, sizeof(uhf->snapshot));
        result->module_ok = true;
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
    size_t tid_len = 0U;
    size_t user_len = 0U;
    uint32_t access_password = 0U;
    uint16_t qt_control = 0U;
    bool qt_known = false;
    bool relock_needed = false;
    if(!uhf || !result || uhf->snapshot.epc_len == 0U) return false;
    if(uhf->snapshot.access_password_len == 4U) {
        access_password = ((uint32_t)uhf->snapshot.access_password[0] << 24U) |
                          ((uint32_t)uhf->snapshot.access_password[1] << 16U) |
                          ((uint32_t)uhf->snapshot.access_password[2] << 8U) |
                          uhf->snapshot.access_password[3];
    }

    if(!seader_uhf_power_on(uhf) || !seader_uhf_open_session(uhf)) return false;
    bool ok = false;
    do {
        if(!seader_uhf_apply_ops_profile(uhf)) break;
        if(uhf->snapshot.public_tid_len == SEADER_UHF_TRACE_TID_LEN) {
            if(!seader_uhf_select_mask(
                   uhf, 0x02U, 0x00000000U, uhf->snapshot.public_tid, uhf->snapshot.public_tid_len)) {
                break;
            }
        } else if(!seader_uhf_select_epc(uhf, uhf->snapshot.epc, uhf->snapshot.epc_len)) {
            break;
        }
        furi_delay_ms(20);

        if(access_password != 0U && seader_uhf_qt_read(uhf, access_password, &qt_control)) {
            qt_known = true;
        }

        SeaderUhfPrivateReadPlan plan =
            seader_uhf_plan_private_read(access_password != 0U, qt_known, qt_control);
        if(!plan.can_read) break;

        if(plan.should_unlock) {
            if(!seader_uhf_qt_write(uhf, access_password, (uint16_t)(qt_control & ~seader_uhf_qt_public_mask))) {
                break;
            }
            relock_needed = true;
            furi_delay_ms(20);
            if(uhf->snapshot.public_tid_len == SEADER_UHF_TRACE_TID_LEN &&
               !seader_uhf_select_mask(
                   uhf, 0x02U, 0x00000000U, uhf->snapshot.public_tid, uhf->snapshot.public_tid_len)) {
                break;
            }
            furi_delay_ms(20);
        }

        if(!seader_uhf_read_memory(
               uhf,
               0U,
               0x02U,
               0x0000U,
               SEADER_UHF_WORDS_TID_READ,
               tid,
               sizeof(tid),
               &tid_len) &&
           !seader_uhf_read_memory(
               uhf,
               access_password,
               0x02U,
               0x0000U,
               SEADER_UHF_WORDS_TID_READ,
               tid,
               sizeof(tid),
               &tid_len)) {
            break;
        }

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
            break;
        }
        memcpy(uhf->snapshot.private_tid, tid, tid_len);
        uhf->snapshot.private_tid_len = tid_len;
        memcpy(uhf->snapshot.user_data, user, user_len);
        uhf->snapshot.user_data_len = user_len;
        ok = true;
    } while(false);

    if(relock_needed) {
        ok = ok && seader_uhf_try_relock_public(uhf, access_password, uhf->snapshot.public_tid);
    }
    seader_uhf_close_session(uhf, false);

    if(ok) {
        result->module_ok = true;
        result->private_data_ok = true;
        memcpy(&result->tag, &uhf->snapshot, sizeof(result->tag));
    }
    return ok;
}

void seader_uhf_hibernate(SeaderUhf* uhf) {
    seader_uhf_power_off(uhf);
}

static bool seader_uhf_send_response_payload(
    Seader* seader,
    Response_t* response,
    const OCTET_STRING_t* request_header) {
    Payload_t payload = {0};
    payload.present = Payload_PR_response;
    payload.choice.response = *response;
    const uint8_t* header = request_header->buf;
    seader_send_payload(seader, &payload, header[1], header[0], 0x00U);
    return true;
}

bool seader_uhf_handle_i2c_command(Seader* seader, SeaderUhf* uhf, const I2CCommand_t* i2c_command) {
    if(!seader || !uhf || !i2c_command || !i2c_command->header.buf || i2c_command->header.size != 6U) {
        return false;
    }
    if(i2c_command->busAddress != SEADER_UHF_BUS_ADDRESS) return false;

    Response_t response = {0};
    response.present = Response_PR_uhfModuleResponse;
    UHFModuleResponse_t* uhf_response = &response.choice.uhfModuleResponse;
    memset(uhf_response, 0, sizeof(*uhf_response));

    switch(i2c_command->uhfModuleCommand.present) {
    case UHFModuleCommand_PR_uhfModuleCommandGetVersion:
        uhf_response->present = UHFModuleResponse_PR_uhfModuleResponseVersion;
        OCTET_STRING_fromBuf(
            &uhf_response->choice.uhfModuleResponseVersion,
            (const char*)uhf->version_raw,
            uhf->version_raw_len);
        break;
    case UHFModuleCommand_PR_uhfModuleCommandGetProperties:
        uhf_response->present = UHFModuleResponse_PR_uhfModuleResponseGetProperties;
        uhf_response->choice.uhfModuleResponseGetProperties.i2cMaxClockSpeedInKHz =
            calloc(1, sizeof(I2CClockSpeed_t));
        *uhf_response->choice.uhfModuleResponseGetProperties.i2cMaxClockSpeedInKHz =
            I2CClockSpeed_i2c100KHz;
        break;
    case UHFModuleCommand_PR_uhfModuleCommandSetAccessPassword:
        uhf_response->present = UHFModuleResponse_PR_uhfModuleResponseSetAccessPassword;
        (void)seader_uhf_set_access_password(
            uhf,
            i2c_command->uhfModuleCommand.choice.uhfModuleCommandSetAccessPassword.buf,
            i2c_command->uhfModuleCommand.choice.uhfModuleCommandSetAccessPassword.size);
        break;
    case UHFModuleCommand_PR_uhfModuleCommandGetPrivateData: {
        SeaderUhfReadResult result = {0};
        uint8_t sam_tid[SEADER_UHF_TRACE_TID_LEN] = {0};
        uint8_t sam_user_data[SEADER_UHF_SAM_USER_DATA_LEN] = {0};
        size_t sam_tid_len = 0U;
        size_t sam_user_data_len = 0U;
        if(!seader_uhf_read_private_data(uhf, &result)) {
            ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_Response, &response);
            return false;
        }
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
            ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_Response, &response);
            return false;
        }
        uhf_response->present = UHFModuleResponse_PR_uhfModuleResponseGetPrivateData;
        OCTET_STRING_fromBuf(
            &uhf_response->choice.uhfModuleResponseGetPrivateData.tid,
            (const char*)sam_tid,
            sam_tid_len);
        OCTET_STRING_fromBuf(
            &uhf_response->choice.uhfModuleResponseGetPrivateData.userData,
            (const char*)sam_user_data,
            sam_user_data_len);
        break;
    }
    default:
        ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_Response, &response);
        return false;
    }

    bool ok = seader_uhf_send_response_payload(seader, &response, &i2c_command->header);
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_Response, &response);
    return ok;
}
