#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "board_power_lifecycle.h"

#define SEADER_UHF_MODULE_VERSION_MAX_LEN 24U
#define SEADER_UHF_MODULE_LABEL_MAX_LEN   48U
#define SEADER_UHF_MODULE_RX_STREAM_MAX   384U
#define SEADER_UHF_MODULE_RX_MAX          96U

typedef enum {
    SeaderUhfModuleStatusNotApplicable = 0,
    SeaderUhfModuleStatusDetecting,
    SeaderUhfModuleStatusPresent,
    SeaderUhfModuleStatusMissing,
    SeaderUhfModuleStatusFault,
} SeaderUhfModuleStatus;

typedef enum {
    SeaderUhfModuleFamilyUnknown = 0,
    SeaderUhfModuleFamilyM100 = 1,
} SeaderUhfModuleFamily;

typedef enum {
    SeaderUhfModuleHardwareUnknown = 0,
    SeaderUhfModuleHardwareM100Unknown = 1,
    SeaderUhfModuleHardwareM100_26dBm = 2,
} SeaderUhfModuleHardwareClass;

typedef struct {
    SeaderUhfModuleFamily family;
    SeaderUhfModuleHardwareClass hardware_class;
    char hw_version[SEADER_UHF_MODULE_VERSION_MAX_LEN];
    char sw_version[SEADER_UHF_MODULE_VERSION_MAX_LEN];
} SeaderUhfModuleInfo;

extern const uint8_t seader_uhf_module_cmd_hw_version[8];
extern const uint8_t seader_uhf_module_cmd_sw_version[8];

void seader_uhf_module_info_reset(SeaderUhfModuleInfo* info);
void seader_uhf_module_info_set_versions(
    SeaderUhfModuleInfo* info,
    const uint8_t* hw_payload,
    size_t hw_payload_len,
    const uint8_t* sw_payload,
    size_t sw_payload_len);

uint8_t seader_uhf_module_frame_checksum(const uint8_t* data, size_t len);
bool seader_uhf_module_try_parse_frame(
    const uint8_t* stream,
    size_t stream_len,
    uint8_t expected_type,
    uint8_t expected_cmd,
    uint8_t* payload,
    size_t payload_cap,
    size_t* payload_len);

void seader_uhf_module_status_label_format(
    SeaderBoardAttachment attachment,
    SeaderUhfModuleStatus status,
    const SeaderUhfModuleInfo* info,
    char* out,
    size_t out_size);
