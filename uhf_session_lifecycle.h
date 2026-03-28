#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    SeaderUhfExpansionStateUnknown = 0,
    SeaderUhfExpansionStateDisabled,
    SeaderUhfExpansionStateEnabled,
    SeaderUhfExpansionStateRunning,
} SeaderUhfExpansionState;

typedef struct {
    bool should_disable;
    bool should_restore;
} SeaderUhfExpansionAcquirePlan;

typedef struct {
    bool should_enable_otg;
    bool owns_otg;
    bool use_external_vbus;
} SeaderUhfPowerAcquirePlan;

SeaderUhfExpansionAcquirePlan seader_uhf_plan_expansion_acquire(SeaderUhfExpansionState state);
SeaderUhfPowerAcquirePlan seader_uhf_plan_power_acquire(bool otg_enabled, uint16_t vbus_mv);

bool seader_uhf_should_restore_expansion(bool restore_needed);
bool seader_uhf_should_disable_owned_otg(bool power_owned, bool otg_enabled);
bool seader_uhf_power_is_available(bool otg_enabled, uint16_t vbus_mv);
bool seader_uhf_power_fault_is_fatal(bool otg_fault, uint16_t vbus_mv);
bool seader_uhf_should_power_off_after_probe(bool temporary_power, bool detected);
bool seader_uhf_should_ack_access_password(size_t key_len);
