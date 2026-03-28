#include "uhf_session_lifecycle.h"

#define SEADER_UHF_POWER_AVAILABLE_MV 4500U

SeaderUhfExpansionAcquirePlan seader_uhf_plan_expansion_acquire(SeaderUhfExpansionState state) {
    SeaderUhfExpansionAcquirePlan plan = {
        .should_disable = false,
        .should_restore = false,
    };

    switch(state) {
    case SeaderUhfExpansionStateDisabled:
        break;
    case SeaderUhfExpansionStateEnabled:
    case SeaderUhfExpansionStateRunning:
    case SeaderUhfExpansionStateUnknown:
    default:
        plan.should_disable = true;
        plan.should_restore = true;
        break;
    }

    return plan;
}

SeaderUhfPowerAcquirePlan seader_uhf_plan_power_acquire(bool otg_enabled, uint16_t vbus_mv) {
    SeaderUhfPowerAcquirePlan plan = {
        .should_enable_otg = false,
        .owns_otg = false,
        .use_external_vbus = false,
    };

    if(otg_enabled) {
        return plan;
    }

    if(vbus_mv >= SEADER_UHF_POWER_AVAILABLE_MV) {
        plan.use_external_vbus = true;
        return plan;
    }

    plan.should_enable_otg = true;
    plan.owns_otg = true;
    return plan;
}

bool seader_uhf_should_restore_expansion(bool restore_needed) {
    return restore_needed;
}

bool seader_uhf_should_disable_owned_otg(bool power_owned, bool otg_enabled) {
    return power_owned && otg_enabled;
}

bool seader_uhf_power_is_available(bool otg_enabled, uint16_t vbus_mv) {
    return otg_enabled || vbus_mv >= SEADER_UHF_POWER_AVAILABLE_MV;
}

bool seader_uhf_power_fault_is_fatal(bool otg_fault, uint16_t vbus_mv) {
    return otg_fault && vbus_mv < SEADER_UHF_POWER_AVAILABLE_MV;
}

bool seader_uhf_should_power_off_after_probe(bool temporary_power, bool detected) {
    return temporary_power && !detected;
}

bool seader_uhf_should_ack_access_password(size_t key_len) {
    return key_len == 4U;
}
