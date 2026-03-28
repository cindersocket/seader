#include "uhf_logic.h"

#include <string.h>

static const uint8_t seader_uhf_public_epc_prefix[6] = {0x00U, 0x00U, 0x72U, 0x38U, 0x00U, 0x00U};
static const uint8_t seader_uhf_public_tid_prefix[6] = {0xE2U, 0x80U, 0x11U, 0x05U, 0x00U, 0x00U};
static const uint8_t seader_uhf_private_tid_prefix[6] = {0xE2U, 0x80U, 0x11U, 0x05U, 0x20U, 0x00U};

bool seader_uhf_is_public_epc_form(const uint8_t* value, size_t value_len) {
    return value && value_len == 12U &&
           memcmp(value, seader_uhf_public_epc_prefix, sizeof(seader_uhf_public_epc_prefix)) == 0;
}

bool seader_uhf_extract_serial_from_form(
    const uint8_t* value,
    size_t value_len,
    uint8_t serial_out[6]) {
    static const uint8_t sam_csn_prefix[10] = {
        0xE2U, 0x80U, 0x11U, 0x05U, 0x00U, 0x00U, 0x72U, 0x38U, 0x00U, 0x00U};

    if(!value || !serial_out) return false;

    if(value_len == 12U) {
        if(memcmp(value, seader_uhf_public_epc_prefix, sizeof(seader_uhf_public_epc_prefix)) ==
               0U ||
           memcmp(value, seader_uhf_public_tid_prefix, sizeof(seader_uhf_public_tid_prefix)) ==
               0U ||
           memcmp(value, seader_uhf_private_tid_prefix, sizeof(seader_uhf_private_tid_prefix)) ==
               0U) {
            memcpy(serial_out, value + 6U, 6U);
            return true;
        }
    } else if(
        value_len == SEADER_UHF_NORMALIZED_CSN_LEN &&
        memcmp(value, sam_csn_prefix, sizeof(sam_csn_prefix)) == 0U) {
        memcpy(serial_out, value + sizeof(sam_csn_prefix), 6U);
        return true;
    }

    return false;
}

bool seader_uhf_build_sam_csn_from_serial(
    const uint8_t serial[6],
    uint8_t* csn_out,
    size_t csn_out_cap,
    size_t* csn_out_len) {
    static const uint8_t prefix[10] = {
        0xE2U, 0x80U, 0x11U, 0x05U, 0x00U, 0x00U, 0x72U, 0x38U, 0x00U, 0x00U};

    if(!serial || !csn_out || !csn_out_len || csn_out_cap < SEADER_UHF_NORMALIZED_CSN_LEN) {
        return false;
    }

    memcpy(csn_out, prefix, sizeof(prefix));
    memcpy(csn_out + sizeof(prefix), serial, 6U);
    *csn_out_len = SEADER_UHF_NORMALIZED_CSN_LEN;
    return true;
}

bool seader_uhf_build_sam_csn_from_public_tid(
    const uint8_t* public_tid,
    size_t public_tid_len,
    uint8_t* csn_out,
    size_t csn_out_cap,
    size_t* csn_out_len) {
    uint8_t serial[6] = {0};

    if(!public_tid || !csn_out || !csn_out_len || csn_out_cap < SEADER_UHF_NORMALIZED_CSN_LEN) {
        return false;
    }
    if(public_tid_len < SEADER_UHF_TRACE_TID_LEN) return false;

    memcpy(serial, public_tid + 6U, sizeof(serial));
    return seader_uhf_build_sam_csn_from_serial(serial, csn_out, csn_out_cap, csn_out_len);
}

const char* seader_uhf_describe_form(const uint8_t* value, size_t value_len) {
    if(!value || value_len != 12U) {
        return "workorder-or-unknown";
    }

    if(seader_uhf_is_public_epc_form(value, value_len)) {
        return "public-epc";
    }

    if(memcmp(value, seader_uhf_public_tid_prefix, sizeof(seader_uhf_public_tid_prefix)) == 0U) {
        return "public-tid";
    }

    if(memcmp(value, seader_uhf_private_tid_prefix, sizeof(seader_uhf_private_tid_prefix)) == 0U) {
        return "private-tid";
    }

    return "workorder-or-unknown";
}

bool seader_uhf_build_sam_csn_from_form(
    const uint8_t* value,
    size_t value_len,
    uint8_t* csn_out,
    size_t csn_out_cap,
    size_t* csn_out_len) {
    uint8_t serial[6] = {0};

    if(!seader_uhf_extract_serial_from_form(value, value_len, serial)) {
        return false;
    }

    return seader_uhf_build_sam_csn_from_serial(serial, csn_out, csn_out_cap, csn_out_len);
}

bool seader_uhf_build_private_tid_from_form(
    const uint8_t* value,
    size_t value_len,
    uint8_t* tid_out,
    size_t tid_out_cap,
    size_t* tid_out_len) {
    uint8_t serial[6] = {0};

    if(!tid_out || !tid_out_len || tid_out_cap < SEADER_UHF_TRACE_TID_LEN) {
        return false;
    }

    if(!seader_uhf_extract_serial_from_form(value, value_len, serial)) {
        return false;
    }

    memcpy(tid_out, seader_uhf_private_tid_prefix, sizeof(seader_uhf_private_tid_prefix));
    memcpy(tid_out + sizeof(seader_uhf_private_tid_prefix), serial, sizeof(serial));
    *tid_out_len = SEADER_UHF_TRACE_TID_LEN;
    return true;
}

bool seader_uhf_extract_trace_tid_from_tid_bank(
    const uint8_t* tid_bank,
    size_t tid_bank_len,
    uint8_t* tid_out,
    size_t tid_out_cap,
    size_t* tid_out_len) {
    if(!tid_bank || !tid_out || !tid_out_len || tid_out_cap < SEADER_UHF_TRACE_TID_LEN) {
        return false;
    }

    if(tid_bank_len < SEADER_UHF_TRACE_TID_LEN) {
        return false;
    }

    memcpy(tid_out, tid_bank, SEADER_UHF_TRACE_TID_LEN);
    *tid_out_len = SEADER_UHF_TRACE_TID_LEN;
    return seader_uhf_classify_tid_view(tid_out, *tid_out_len) != SeaderUhfTidViewUnknown;
}

SeaderUhfTidView seader_uhf_classify_tid_view(const uint8_t* tid, size_t tid_len) {
    if(!tid || tid_len < SEADER_UHF_TRACE_TID_LEN) return SeaderUhfTidViewUnknown;

    if(tid[4] == 0x00U && tid[5] == 0x00U) return SeaderUhfTidViewPublic;
    if(tid[4] == 0x20U && tid[5] == 0x00U) return SeaderUhfTidViewPrivate;
    return SeaderUhfTidViewUnknown;
}

SeaderUhfPrivateReadPlan
    seader_uhf_plan_private_read(bool have_access_password, bool qt_known, uint16_t qt_control) {
    SeaderUhfPrivateReadPlan plan = {0};

    if(!have_access_password) return plan;

    plan.can_read = true;
    plan.mode = SeaderUhfPrivateReadModeDirect;
    plan.should_relock = true;

    if(qt_known && (qt_control & 0x4000U) != 0U) {
        plan.should_unlock = true;
        plan.should_relock = true;
        plan.mode = SeaderUhfPrivateReadModeUnlockThenRead;
    }

    return plan;
}

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
    size_t* user_data_out_len) {
    if(!tid || !tid_out || !tid_out_len || !user_data_out || !user_data_out_len) return false;
    if(tid_len < SEADER_UHF_TRACE_TID_LEN || tid_out_cap < SEADER_UHF_TRACE_TID_LEN) return false;
    if(user_data_out_cap < SEADER_UHF_SAM_USER_DATA_LEN) return false;

    memcpy(tid_out, tid, SEADER_UHF_TRACE_TID_LEN);
    *tid_out_len = SEADER_UHF_TRACE_TID_LEN;

    memset(user_data_out, 0, SEADER_UHF_SAM_USER_DATA_LEN);
    if(user_data && user_data_len > 0U) {
        size_t copy_len = user_data_len;
        if(copy_len > SEADER_UHF_SAM_USER_DATA_LEN) copy_len = SEADER_UHF_SAM_USER_DATA_LEN;
        memcpy(user_data_out, user_data, copy_len);
    }
    *user_data_out_len = SEADER_UHF_SAM_USER_DATA_LEN;
    return true;
}

bool seader_uhf_inventory_identification_complete(
    bool tag_found,
    bool select_ok,
    bool tid_read_ok,
    bool csn_derived_ok) {
    (void)tid_read_ok;
    return tag_found && select_ok && csn_derived_ok;
}
