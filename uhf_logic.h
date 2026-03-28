#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SEADER_UHF_NORMALIZED_CSN_LEN 16U
#define SEADER_UHF_TRACE_TID_LEN      12U
#define SEADER_UHF_SAM_USER_DATA_LEN  64U

typedef enum {
    SeaderUhfTidViewUnknown = 0,
    SeaderUhfTidViewPublic,
    SeaderUhfTidViewPrivate,
} SeaderUhfTidView;

typedef enum {
    SeaderUhfPrivateReadModeDirect = 0,
    SeaderUhfPrivateReadModeUnlockThenRead,
} SeaderUhfPrivateReadMode;

typedef struct {
    bool can_read;
    bool should_unlock;
    bool should_relock;
    SeaderUhfPrivateReadMode mode;
} SeaderUhfPrivateReadPlan;

bool seader_uhf_is_public_epc_form(const uint8_t* value, size_t value_len);

bool seader_uhf_extract_serial_from_form(
    const uint8_t* value,
    size_t value_len,
    uint8_t serial_out[6]);

bool seader_uhf_build_sam_csn_from_serial(
    const uint8_t serial[6],
    uint8_t* csn_out,
    size_t csn_out_cap,
    size_t* csn_out_len);

bool seader_uhf_build_sam_csn_from_public_tid(
    const uint8_t* public_tid,
    size_t public_tid_len,
    uint8_t* csn_out,
    size_t csn_out_cap,
    size_t* csn_out_len);

bool seader_uhf_build_sam_csn_from_form(
    const uint8_t* value,
    size_t value_len,
    uint8_t* csn_out,
    size_t csn_out_cap,
    size_t* csn_out_len);

bool seader_uhf_build_private_tid_from_form(
    const uint8_t* value,
    size_t value_len,
    uint8_t* tid_out,
    size_t tid_out_cap,
    size_t* tid_out_len);

bool seader_uhf_extract_trace_tid_from_tid_bank(
    const uint8_t* tid_bank,
    size_t tid_bank_len,
    uint8_t* tid_out,
    size_t tid_out_cap,
    size_t* tid_out_len);

const char* seader_uhf_describe_form(const uint8_t* value, size_t value_len);

SeaderUhfTidView seader_uhf_classify_tid_view(const uint8_t* tid, size_t tid_len);

SeaderUhfPrivateReadPlan
    seader_uhf_plan_private_read(bool have_access_password, bool qt_known, uint16_t qt_control);

bool seader_uhf_normalize_private_data_for_sam(
    const uint8_t* tid,
    size_t tid_len,
    const uint8_t* user_data,
    size_t user_data_len,
    uint8_t* tid_out,
    size_t tid_out_cap,
    size_t* tid_out_len,
    uint8_t* user_data_out,
    size_t user_data_out_cap,
    size_t* user_data_out_len);

bool seader_uhf_inventory_identification_complete(
    bool tag_found,
    bool select_ok,
    bool tid_read_ok,
    bool csn_derived_ok);
