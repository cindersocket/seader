#include "uhf_logic.h"

#include <string.h>

bool seader_uhf_build_sam_csn_from_public_tid(
    const uint8_t* public_tid,
    size_t public_tid_len,
    uint8_t* csn_out,
    size_t csn_out_cap,
    size_t* csn_out_len) {
    static const uint8_t prefix[10] = {
        0xE2U, 0x80U, 0x11U, 0x05U, 0x00U, 0x00U, 0x72U, 0x38U, 0x00U, 0x00U};

    if(!public_tid || !csn_out || !csn_out_len || csn_out_cap < SEADER_UHF_NORMALIZED_CSN_LEN) {
        return false;
    }
    if(public_tid_len < SEADER_UHF_TRACE_TID_LEN) return false;

    for(size_t i = 0; i < sizeof(prefix); i++) {
        csn_out[i] = prefix[i];
    }
    for(size_t i = 0; i < 6U; i++) {
        csn_out[sizeof(prefix) + i] = public_tid[6U + i];
    }
    *csn_out_len = SEADER_UHF_NORMALIZED_CSN_LEN;
    return true;
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
