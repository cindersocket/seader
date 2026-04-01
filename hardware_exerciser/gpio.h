#pragma once

#include "protocol.h"

#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_resources.h>

#define HE_GPIO_MAX_PINS 24U

typedef struct {
    const GpioPinRecord* record;
    uint8_t mode;
    uint8_t pull;
    bool configured;
} HeGpioPinState;

typedef struct {
    HeGpioPinState pins[HE_GPIO_MAX_PINS];
    size_t pin_count;
    FuriHalAdcHandle* adc_handle;
} HeGpioState;

bool he_gpio_init(HeGpioState* state);
void he_gpio_free(HeGpioState* state);
void he_gpio_reset_all(HeGpioState* state);
void he_gpio_log_pin_summary(HeGpioState* state);

HxStatus he_gpio_list_pins(HeGpioState* state, uint8_t* out, size_t out_cap, size_t* out_len);
HxStatus he_gpio_configure(
    HeGpioState* state,
    const HxRequest* request,
    uint8_t* out,
    size_t out_cap,
    size_t* out_len);
HxStatus he_gpio_write(
    HeGpioState* state,
    const HxRequest* request,
    uint8_t* out,
    size_t out_cap,
    size_t* out_len);
HxStatus he_gpio_read(
    HeGpioState* state,
    const HxRequest* request,
    uint8_t* out,
    size_t out_cap,
    size_t* out_len);
HxStatus he_gpio_read_analog(
    HeGpioState* state,
    const HxRequest* request,
    uint8_t* out,
    size_t out_cap,
    size_t* out_len);
HxStatus he_gpio_vector_capture(
    HeGpioState* state,
    const HxRequest* request,
    uint8_t* out,
    size_t out_cap,
    size_t* out_len);
