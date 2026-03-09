#include "uhf_capability.h"

#include <string.h>

bool seader_uhf_should_show_root_menu(bool module_present, SeaderUhfSamSupport sam_support) {
    return module_present || sam_support == SeaderUhfSamSupportSupported;
}

bool seader_uhf_should_show_root_unavailable(bool module_present, SeaderUhfSamSupport sam_support) {
    return !module_present && sam_support == SeaderUhfSamSupportUnavailable;
}

bool seader_uhf_root_leads_to_no_module(bool module_present, SeaderUhfSamSupport sam_support) {
    return !module_present && sam_support == SeaderUhfSamSupportSupported;
}

const char* seader_uhf_sio_menu_label(bool sam_present, SeaderUhfSamSupport sam_support) {
    if(!sam_present) return "No SAM";

    switch(sam_support) {
    case SeaderUhfSamSupportSupported:
        return "Read SIO";
    case SeaderUhfSamSupportUnavailable:
        return "SAM lacks UHF support";
    case SeaderUhfSamSupportUnknown:
    default:
        return "Checking SAM UHF support";
    }
}

bool seader_uhf_sio_menu_enabled(bool sam_present, SeaderUhfSamSupport sam_support) {
    return sam_present && sam_support == SeaderUhfSamSupportSupported;
}

bool seader_uhf_read_mode_is_raw_result(SeaderUhfReadMode mode) {
    return mode == SeaderUhfReadModeEpc || mode == SeaderUhfReadModeTid;
}

SeaderUhfSamSupport
    seader_uhf_sam_support_from_key_probe(bool monza_supported, bool higgs_supported) {
    return (monza_supported || higgs_supported) ? SeaderUhfSamSupportSupported :
                                                  SeaderUhfSamSupportUnavailable;
}

SeaderUhfSnmpProbeResult
    seader_uhf_snmp_classify_probe_error(uint32_t error_code, const uint8_t* data, size_t len) {
    uint8_t d0 = len > 0U ? data[0] : 0x00U;
    uint8_t d1 = len > 1U ? data[1] : 0x00U;

    if((error_code == 0x06U && len >= 2U && d0 == 0x69U && d1 == 0x82U) ||
       (error_code == 0x11U && len >= 2U && d0 == 0x38U && d1 == 0x00U)) {
        return SeaderUhfSnmpProbeProtected;
    }

    if(error_code == 0x11U && len >= 2U &&
       ((d0 == 0x2EU && d1 == 0x00U) || (d0 == 0x39U && d1 == 0x00U))) {
        return SeaderUhfSnmpProbeMissing;
    }

    return SeaderUhfSnmpProbeUnexpected;
}

bool seader_uhf_is_elite_ice1803(const uint8_t* value, size_t len) {
    static const uint8_t elite_ice1803[] = {'I', 'C', 'E', '1', '8', '0', '3'};
    return value && len == sizeof(elite_ice1803) &&
           memcmp(value, elite_ice1803, sizeof(elite_ice1803)) == 0;
}
