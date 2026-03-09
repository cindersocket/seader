#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    SeaderUhfSamSupportUnknown,
    SeaderUhfSamSupportUnavailable,
    SeaderUhfSamSupportSupported,
} SeaderUhfSamSupport;

typedef enum {
    SeaderUhfSnmpProbeMissing,
    SeaderUhfSnmpProbeProtected,
    SeaderUhfSnmpProbeUnexpected,
} SeaderUhfSnmpProbeResult;

typedef enum {
    SeaderUhfReadModeNone,
    SeaderUhfReadModeSio,
    SeaderUhfReadModeEpc,
    SeaderUhfReadModeTid,
} SeaderUhfReadMode;

bool seader_uhf_should_show_root_menu(bool module_present, SeaderUhfSamSupport sam_support);
const char* seader_uhf_sio_menu_label(bool sam_present, SeaderUhfSamSupport sam_support);
bool seader_uhf_sio_menu_enabled(bool sam_present, SeaderUhfSamSupport sam_support);
bool seader_uhf_read_mode_is_raw_result(SeaderUhfReadMode mode);
SeaderUhfSamSupport
    seader_uhf_sam_support_from_key_probe(bool monza_supported, bool higgs_supported);
SeaderUhfSnmpProbeResult
    seader_uhf_snmp_classify_probe_error(uint32_t error_code, const uint8_t* data, size_t len);
bool seader_uhf_is_elite_ice1803(const uint8_t* value, size_t len);
