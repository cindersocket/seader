#include "gpio.h"
#include "log.h"

#include <string.h>

static HeGpioPinState* he_gpio_find_pin(HeGpioState* state, uint8_t pin_number) {
    if(!state) {
        return NULL;
    }

    for(size_t i = 0U; i < state->pin_count; i++) {
        if(state->pins[i].record->number == pin_number) {
            return &state->pins[i];
        }
    }

    return NULL;
}

static bool he_gpio_is_output_mode(uint8_t mode) {
    return mode == HxGpioModeOutputPushPull || mode == HxGpioModeOutputOpenDrain;
}

static bool he_gpio_is_valid_mode(uint8_t mode) {
    return mode <= HxGpioModeAnalog;
}

static bool he_gpio_is_valid_pull(uint8_t pull) {
    return pull <= HxGpioPullDown;
}

static GpioMode he_gpio_hal_mode(uint8_t mode) {
    if(mode == HxGpioModeInput) {
        return GpioModeInput;
    } else if(mode == HxGpioModeOutputPushPull) {
        return GpioModeOutputPushPull;
    } else if(mode == HxGpioModeOutputOpenDrain) {
        return GpioModeOutputOpenDrain;
    }

    return GpioModeAnalog;
}

static GpioPull he_gpio_hal_pull(uint8_t pull) {
    if(pull == HxGpioPullUp) {
        return GpioPullUp;
    } else if(pull == HxGpioPullDown) {
        return GpioPullDown;
    }

    return GpioPullNo;
}

static HxStatus he_gpio_get_analog_handle(HeGpioState* state) {
    if(!state) {
        return HxStatusIoError;
    }

    if(!state->adc_handle) {
        he_log_tag("HEGPIO", "acquiring adc handle");
        state->adc_handle = furi_hal_adc_acquire();
        if(!state->adc_handle) {
            he_log_tag("HEGPIO", "adc acquire failed");
            return HxStatusIoError;
        }
        furi_hal_adc_configure(state->adc_handle);
        he_log_tag("HEGPIO", "adc configured");
    }

    return HxStatusOk;
}

static HxStatus he_gpio_validate_configure_request(
    HeGpioPinState* pin,
    const HxGpioConfigureRequest* request) {
    if(!pin || !request || !he_gpio_is_valid_mode(request->mode) ||
       !he_gpio_is_valid_pull(request->pull) || request->initial_level > 1U) {
        return HxStatusInvalidRequest;
    }

    if(request->mode == HxGpioModeAnalog) {
        if(pin->record->channel == FuriHalAdcChannelNone || request->pull != HxGpioPullNone) {
            return HxStatusInvalidRequest;
        }
    }

    return HxStatusOk;
}

static void he_gpio_apply_default(HeGpioPinState* pin) {
    furi_hal_gpio_init(pin->record->pin, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
    pin->mode = HxGpioModeAnalog;
    pin->pull = HxGpioPullNone;
    pin->configured = false;
}

void he_gpio_log_pin_summary(HeGpioState* state) {
    if(!state) {
        return;
    }

    he_log_tag("HEGPIO", "exposed pin_count=%u", (unsigned)state->pin_count);
    for(size_t i = 0U; i < state->pin_count; i++) {
        he_log_tag(
            "HEGPIO",
            "pin[%u] num=%u name=%s analog=%u",
            (unsigned)i,
            state->pins[i].record->number,
            state->pins[i].record->name,
            state->pins[i].record->channel != FuriHalAdcChannelNone ? 1U : 0U);
    }
}

static HxStatus he_gpio_sample_analog(
    HeGpioState* state,
    HeGpioPinState* pin,
    uint16_t* raw_value,
    uint16_t* millivolts) {
    if(!state || !pin || !raw_value || !millivolts) {
        return HxStatusIoError;
    }

    if(!pin->configured || pin->mode != HxGpioModeAnalog ||
       pin->record->channel == FuriHalAdcChannelNone) {
        return HxStatusInvalidRequest;
    }

    const HxStatus status = he_gpio_get_analog_handle(state);
    if(status != HxStatusOk) {
        return status;
    }

    *raw_value = furi_hal_adc_read(state->adc_handle, pin->record->channel);
    const float mv = furi_hal_adc_convert_to_voltage(state->adc_handle, *raw_value);
    *millivolts = (mv <= 0.0f) ? 0U : (mv >= 65535.0f ? 65535U : (uint16_t)(mv + 0.5f));
    return HxStatusOk;
}

bool he_gpio_init(HeGpioState* state) {
    if(!state) {
        return false;
    }

    memset(state, 0, sizeof(*state));
    he_log_tag("HEGPIO", "initializing gpio state from %u firmware pins", (unsigned)gpio_pins_count);
    for(size_t i = 0U; i < gpio_pins_count; i++) {
        if(gpio_pins[i].debug || gpio_pins[i].number == 0U) {
            continue;
        }

        if(state->pin_count >= HE_GPIO_MAX_PINS) {
            return false;
        }

        state->pins[state->pin_count++].record = &gpio_pins[i];
    }

    he_gpio_reset_all(state);
    he_gpio_log_pin_summary(state);
    return true;
}

void he_gpio_free(HeGpioState* state) {
    if(!state) {
        return;
    }

    he_gpio_reset_all(state);
    if(state->adc_handle) {
        he_log_tag("HEGPIO", "releasing adc handle");
        furi_hal_adc_release(state->adc_handle);
        state->adc_handle = NULL;
    }
}

void he_gpio_reset_all(HeGpioState* state) {
    if(!state) {
        return;
    }

    he_log_tag("HEGPIO", "resetting %u pins to default analog mode", (unsigned)state->pin_count);
    for(size_t i = 0U; i < state->pin_count; i++) {
        he_gpio_apply_default(&state->pins[i]);
    }
}

HxStatus he_gpio_list_pins(HeGpioState* state, uint8_t* out, size_t out_cap, size_t* out_len) {
    if(!state || !out || !out_len || out_cap == 0U) {
        return HxStatusIoError;
    }

    out[0] = (uint8_t)state->pin_count;
    size_t used = 1U;
    for(size_t i = 0U; i < state->pin_count; i++) {
        uint8_t flags = HX_GPIO_PIN_FLAG_DIGITAL;
        if(state->pins[i].record->channel != FuriHalAdcChannelNone) {
            flags |= HX_GPIO_PIN_FLAG_ANALOG;
        }

        const size_t entry_len = hx_build_gpio_pin_info(
            state->pins[i].record->number,
            flags,
            state->pins[i].record->name,
            out + used,
            out_cap - used);
        if(entry_len == 0U) {
            *out_len = 0U;
            return HxStatusIoError;
        }
        used += entry_len;
    }

    *out_len = used;
    return HxStatusOk;
}

HxStatus he_gpio_configure(
    HeGpioState* state,
    const HxRequest* request,
    uint8_t* out,
    size_t out_cap,
    size_t* out_len) {
    HxGpioConfigureRequest configure = {0};
    UNUSED(out);
    UNUSED(out_cap);

    if(!state || !request || !out_len ||
       !hx_parse_gpio_configure_request(request->body, request->body_len, &configure)) {
        return HxStatusInvalidRequest;
    }

    HeGpioPinState* pin = he_gpio_find_pin(state, configure.pin_number);
    if(!pin) {
        return HxStatusNotFound;
    }

    const HxStatus validate_status = he_gpio_validate_configure_request(pin, &configure);
    if(validate_status != HxStatusOk) {
        return validate_status;
    }

    furi_hal_gpio_init(
        pin->record->pin,
        he_gpio_hal_mode(configure.mode),
        he_gpio_hal_pull(configure.pull),
        configure.mode == HxGpioModeAnalog ? GpioSpeedLow : GpioSpeedVeryHigh);
    if(he_gpio_is_output_mode(configure.mode)) {
        furi_hal_gpio_write(pin->record->pin, configure.initial_level != 0U);
    }

    pin->mode = configure.mode;
    pin->pull = configure.pull;
    pin->configured = true;
    he_log_tag(
        "HEGPIO",
        "configure pin=%u mode=%u pull=%u initial=%u",
        configure.pin_number,
        configure.mode,
        configure.pull,
        configure.initial_level);
    *out_len = 0U;
    return HxStatusOk;
}

HxStatus he_gpio_write(
    HeGpioState* state,
    const HxRequest* request,
    uint8_t* out,
    size_t out_cap,
    size_t* out_len) {
    HxGpioWriteRequest write = {0};
    UNUSED(out);
    UNUSED(out_cap);

    if(!state || !request || !out_len ||
       !hx_parse_gpio_write_request(request->body, request->body_len, &write) || write.level > 1U) {
        return HxStatusInvalidRequest;
    }

    HeGpioPinState* pin = he_gpio_find_pin(state, write.pin_number);
    if(!pin) {
        return HxStatusNotFound;
    }
    if(!pin->configured || !he_gpio_is_output_mode(pin->mode)) {
        return HxStatusInvalidRequest;
    }

    furi_hal_gpio_write(pin->record->pin, write.level != 0U);
    he_log_tag("HEGPIO", "write pin=%u level=%u", write.pin_number, write.level);
    *out_len = 0U;
    return HxStatusOk;
}

HxStatus he_gpio_read(
    HeGpioState* state,
    const HxRequest* request,
    uint8_t* out,
    size_t out_cap,
    size_t* out_len) {
    uint8_t pin_number = 0U;

    if(!state || !request || !out || !out_len || out_cap < 1U ||
       !hx_parse_gpio_pin_request(request->body, request->body_len, &pin_number)) {
        return HxStatusInvalidRequest;
    }

    HeGpioPinState* pin = he_gpio_find_pin(state, pin_number);
    if(!pin) {
        return HxStatusNotFound;
    }
    if(!pin->configured || pin->mode == HxGpioModeAnalog) {
        return HxStatusInvalidRequest;
    }

    out[0] = furi_hal_gpio_read(pin->record->pin) ? 1U : 0U;
    he_log_tag("HEGPIO", "read pin=%u level=%u", pin_number, out[0]);
    *out_len = 1U;
    return HxStatusOk;
}

HxStatus he_gpio_read_analog(
    HeGpioState* state,
    const HxRequest* request,
    uint8_t* out,
    size_t out_cap,
    size_t* out_len) {
    uint8_t pin_number = 0U;

    if(!state || !request || !out || !out_len ||
       !hx_parse_gpio_pin_request(request->body, request->body_len, &pin_number)) {
        return HxStatusInvalidRequest;
    }

    HeGpioPinState* pin = he_gpio_find_pin(state, pin_number);
    if(!pin) {
        return HxStatusNotFound;
    }

    uint16_t raw_value = 0U;
    uint16_t millivolts = 0U;
    const HxStatus status = he_gpio_sample_analog(state, pin, &raw_value, &millivolts);
    if(status != HxStatusOk) {
        return status;
    }

    const size_t payload_len = hx_build_gpio_analog_value(raw_value, millivolts, out, out_cap);
    if(payload_len == 0U) {
        *out_len = 0U;
        return HxStatusIoError;
    }

    *out_len = payload_len;
    he_log_tag(
        "HEGPIO", "analog pin=%u raw=%u mv=%u", pin_number, raw_value, millivolts);
    return HxStatusOk;
}

HxStatus he_gpio_vector_capture(
    HeGpioState* state,
    const HxRequest* request,
    uint8_t* out,
    size_t out_cap,
    size_t* out_len) {
    HxGpioVectorRequest vector = {0};
    if(!state || !request || !out || !out_len ||
       !hx_parse_gpio_vector_request(request->body, request->body_len, &vector) || out_cap == 0U ||
       vector.sample_count > 255U) {
        return HxStatusInvalidRequest;
    }

    for(size_t i = 0U; i < vector.write_count; i++) {
        HxGpioWriteRequest write = {0};
        hx_gpio_vector_write_at(&vector, i, &write);
        HeGpioPinState* pin = he_gpio_find_pin(state, write.pin_number);
        if(!pin) {
            return HxStatusNotFound;
        }
        if(write.level > 1U || !pin->configured || !he_gpio_is_output_mode(pin->mode)) {
            return HxStatusInvalidRequest;
        }
    }

    for(size_t i = 0U; i < vector.sample_count; i++) {
        HxGpioVectorSample sample = {0};
        hx_gpio_vector_sample_at(&vector, i, &sample);
        HeGpioPinState* pin = he_gpio_find_pin(state, sample.pin_number);
        if(!pin) {
            return HxStatusNotFound;
        }
        if(sample.sample_kind == HxGpioSampleDigital) {
            if(!pin->configured || pin->mode == HxGpioModeAnalog) {
                return HxStatusInvalidRequest;
            }
        } else if(sample.sample_kind == HxGpioSampleAnalog) {
            uint16_t raw_value = 0U;
            uint16_t millivolts = 0U;
            const HxStatus status = he_gpio_sample_analog(state, pin, &raw_value, &millivolts);
            if(status != HxStatusOk) {
                return status;
            }
        } else {
            return HxStatusInvalidRequest;
        }
    }

    for(size_t i = 0U; i < vector.write_count; i++) {
        HxGpioWriteRequest write = {0};
        hx_gpio_vector_write_at(&vector, i, &write);
        HeGpioPinState* pin = he_gpio_find_pin(state, write.pin_number);
        furi_hal_gpio_write(pin->record->pin, write.level != 0U);
    }

    if(vector.settle_us > 0U) {
        furi_delay_us(vector.settle_us);
    }

    out[0] = (uint8_t)vector.sample_count;
    size_t used = 1U;
    for(size_t i = 0U; i < vector.sample_count; i++) {
        HxGpioVectorSample sample = {0};
        hx_gpio_vector_sample_at(&vector, i, &sample);
        HeGpioPinState* pin = he_gpio_find_pin(state, sample.pin_number);
        uint16_t value = 0U;
        uint16_t aux = 0U;

        if(sample.sample_kind == HxGpioSampleDigital) {
            value = furi_hal_gpio_read(pin->record->pin) ? 1U : 0U;
        } else {
            const HxStatus status = he_gpio_sample_analog(state, pin, &value, &aux);
            if(status != HxStatusOk) {
                *out_len = 0U;
                return status;
            }
        }

        const size_t sample_len = hx_build_gpio_vector_sample(
            sample.pin_number, sample.sample_kind, value, aux, out + used, out_cap - used);
        if(sample_len == 0U) {
            *out_len = 0U;
            return HxStatusIoError;
        }
        used += sample_len;
    }

    *out_len = used;
    he_log_tag(
        "HEGPIO",
        "vector writes=%u samples=%u settle_us=%lu response_len=%u",
        (unsigned)vector.write_count,
        (unsigned)vector.sample_count,
        (unsigned long)vector.settle_us,
        (unsigned)*out_len);
    return HxStatusOk;
}
