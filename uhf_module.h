#pragma once

#include "seader_bridge.h"
#include "uhf_interface_fal/uhf_interface.h"
#include "uhf_logic.h"

#define SEADER_UHF_MODULE_VERSION_MAX_LEN 48U
#define SEADER_UHF_EPC_MAX_LEN            32U
#define SEADER_UHF_TID_MAX_LEN            32U
#define SEADER_UHF_USER_DATA_MAX_LEN      80U

typedef enum {
    SeaderUhfModulePresenceAbsent = 0,
    SeaderUhfModulePresencePresent = 1,
} SeaderUhfModulePresence;

typedef enum {
    SeaderUhfModuleFamilyUnknown = 0,
    SeaderUhfModuleFamilyM100,
    SeaderUhfModuleFamilyQM100,
} SeaderUhfModuleFamily;

typedef enum {
    SeaderUhfHardwareClassUnknown = 0,
    SeaderUhfHardwareClass20dBm,
    SeaderUhfHardwareClass26dBm,
    SeaderUhfHardwareClass30dBm,
} SeaderUhfHardwareClass;

typedef struct {
    SeaderUhfModuleFamily family;
    SeaderUhfHardwareClass hardware_class;
    uint32_t baudrate;
    uint16_t query_word;
    uint8_t inventory_mode;
} SeaderUhfModuleProfile;

typedef struct {
    uint8_t epc[SEADER_UHF_EPC_MAX_LEN];
    size_t epc_len;
    uint8_t private_epc[SEADER_UHF_EPC_MAX_LEN];
    size_t private_epc_len;
    uint8_t public_tid[SEADER_UHF_TID_MAX_LEN];
    size_t public_tid_len;
    uint8_t private_tid[SEADER_UHF_TID_MAX_LEN];
    size_t private_tid_len;
    uint8_t sam_csn[SEADER_UHF_NORMALIZED_CSN_LEN];
    size_t sam_csn_len;
    uint8_t user_data[SEADER_UHF_USER_DATA_MAX_LEN];
    size_t user_data_len;
    uint8_t access_password[4];
    size_t access_password_len;
} SeaderUhfTagSnapshot;

typedef struct {
    bool module_ok;
    bool private_data_ok;
    PluginUhfReadFailureReason failure_reason;
    uint16_t debug_stage;
    SeaderUhfTagSnapshot tag;
} SeaderUhfReadResult;

typedef struct SeaderUhf SeaderUhf;

SeaderUhf* seader_uhf_alloc(void);
void seader_uhf_free(SeaderUhf* uhf);

bool seader_uhf_begin(SeaderUhf* uhf);
void seader_uhf_end(SeaderUhf* uhf);
bool seader_uhf_refresh_presence(SeaderUhf* uhf);
bool seader_uhf_is_available(const SeaderUhf* uhf);
const SeaderUhfModuleProfile* seader_uhf_get_profile(const SeaderUhf* uhf);
const SeaderUhfTagSnapshot* seader_uhf_get_snapshot(const SeaderUhf* uhf);
bool seader_uhf_inventory_tag(SeaderUhf* uhf, SeaderUhfReadResult* result);
bool seader_uhf_set_access_password(SeaderUhf* uhf, const uint8_t* key, size_t key_len);
bool seader_uhf_read_private_data(SeaderUhf* uhf, SeaderUhfReadResult* result);
void seader_uhf_hibernate(SeaderUhf* uhf);

bool seader_uhf_build_i2c_response(
    SeaderUhf* uhf,
    const PluginUhfRequest* request,
    PluginUhfResponse* response);
