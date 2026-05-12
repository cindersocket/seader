#include "uhf_module_probe.h"

#include <stdio.h>
#include <string.h>

const uint8_t seader_uhf_module_cmd_hw_version[8] =
    {0xBB, 0x00, 0x03, 0x00, 0x01, 0x00, 0x04, 0x7E};
const uint8_t seader_uhf_module_cmd_sw_version[8] =
    {0xBB, 0x00, 0x03, 0x00, 0x01, 0x01, 0x05, 0x7E};

static void seader_uhf_copy_ascii(
    char* out,
    size_t out_size,
    const uint8_t* payload,
    size_t payload_len) {
    if(!out || out_size == 0U) {
        return;
    }

    out[0] = '\0';
    if(!payload || payload_len == 0U) {
        return;
    }

    const size_t copy_len = payload_len < (out_size - 1U) ? payload_len : (out_size - 1U);
    for(size_t i = 0; i < copy_len; i++) {
        const uint8_t c = payload[i];
        out[i] = (c >= 0x20U && c <= 0x7eU) ? (char)c : '.';
    }
    out[copy_len] = '\0';
}

static void seader_uhf_module_classify(SeaderUhfModuleInfo* info) {
    if(!info) {
        return;
    }

    info->family = SeaderUhfModuleFamilyUnknown;
    info->hardware_class = SeaderUhfModuleHardwareUnknown;

    if(strstr(info->hw_version, "M100")) {
        info->family = SeaderUhfModuleFamilyM100;
        info->hardware_class = SeaderUhfModuleHardwareM100Unknown;
        if(strstr(info->hw_version, "26dBm") || strstr(info->hw_version, "26dBM")) {
            info->hardware_class = SeaderUhfModuleHardwareM100_26dBm;
        }
    }
}

void seader_uhf_module_info_reset(SeaderUhfModuleInfo* info) {
    if(!info) {
        return;
    }
    memset(info, 0, sizeof(*info));
}

void seader_uhf_module_info_set_versions(
    SeaderUhfModuleInfo* info,
    const uint8_t* hw_payload,
    size_t hw_payload_len,
    const uint8_t* sw_payload,
    size_t sw_payload_len) {
    if(!info) {
        return;
    }

    seader_uhf_module_info_reset(info);
    seader_uhf_copy_ascii(info->hw_version, sizeof(info->hw_version), hw_payload, hw_payload_len);
    seader_uhf_copy_ascii(info->sw_version, sizeof(info->sw_version), sw_payload, sw_payload_len);
    seader_uhf_module_classify(info);
}

uint8_t seader_uhf_module_frame_checksum(const uint8_t* data, size_t len) {
    uint8_t sum = 0U;
    if(!data) {
        return 0U;
    }

    for(size_t i = 0U; i < len; i++) {
        sum = (uint8_t)(sum + data[i]);
    }
    return sum;
}

bool seader_uhf_module_try_parse_frame(
    const uint8_t* stream,
    size_t stream_len,
    uint8_t expected_type,
    uint8_t expected_cmd,
    uint8_t* payload,
    size_t payload_cap,
    size_t* payload_len) {
    if(payload_len) {
        *payload_len = 0U;
    }
    if(!stream || !payload || !payload_len) {
        return false;
    }

    for(size_t offset = 0U; offset + 7U <= stream_len; offset++) {
        if(stream[offset] != 0xbbU) {
            continue;
        }

        const size_t body_len = ((size_t)stream[offset + 3U] << 8U) | stream[offset + 4U];
        const size_t frame_len = body_len + 7U;
        if(offset + frame_len > stream_len) {
            continue;
        }
        if(stream[offset + frame_len - 1U] != 0x7eU) {
            continue;
        }
        if(seader_uhf_module_frame_checksum(stream + offset + 1U, frame_len - 3U) !=
           stream[offset + frame_len - 2U]) {
            continue;
        }
        if(stream[offset + 1U] != expected_type || stream[offset + 2U] != expected_cmd) {
            continue;
        }
        if(body_len > payload_cap) {
            continue;
        }

        memcpy(payload, stream + offset + 5U, body_len);
        *payload_len = body_len;
        return true;
    }

    return false;
}

void seader_uhf_module_status_label_format(
    SeaderBoardAttachment attachment,
    SeaderUhfModuleStatus status,
    const SeaderUhfModuleInfo* info,
    char* out,
    size_t out_size) {
    if(!out || out_size == 0U) {
        return;
    }

    out[0] = '\0';
    if(attachment != SeaderBoardAttachmentUhfCarrier ||
       status == SeaderUhfModuleStatusNotApplicable) {
        return;
    }

    switch(status) {
    case SeaderUhfModuleStatusDetecting:
        snprintf(out, out_size, "UHF: detecting...");
        break;
    case SeaderUhfModuleStatusPresent:
        if(info && info->family == SeaderUhfModuleFamilyM100 &&
           info->hardware_class == SeaderUhfModuleHardwareM100_26dBm) {
            snprintf(out, out_size, "UHF: M100 26dBm");
        } else if(info && info->hw_version[0] != '\0') {
            snprintf(out, out_size, "UHF: %.32s", info->hw_version);
        } else {
            snprintf(out, out_size, "UHF: detected");
        }
        break;
    case SeaderUhfModuleStatusMissing:
        snprintf(out, out_size, "UHF: No Module");
        break;
    case SeaderUhfModuleStatusFault:
        snprintf(out, out_size, "UHF: fault");
        break;
    case SeaderUhfModuleStatusNotApplicable:
    default:
        break;
    }
}
