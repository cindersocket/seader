#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    SeaderUhfRoutedCommandNone = 0,
    SeaderUhfRoutedCommandGetVersion,
    SeaderUhfRoutedCommandGetProperties,
    SeaderUhfRoutedCommandSetAccessPassword,
    SeaderUhfRoutedCommandGetPrivateData,
} SeaderUhfRoutedCommand;

typedef struct {
    uint8_t route_header[6];
    uint8_t i2c_header[6];
    uint32_t bus_address;
    SeaderUhfRoutedCommand command;
    uint8_t command_value[32];
    size_t command_value_len;
} SeaderUhfRoutedFrame;

bool seader_uhf_routed_try_parse_apdu(
    const uint8_t* apdu,
    size_t apdu_len,
    SeaderUhfRoutedFrame* frame_out);
