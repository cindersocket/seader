#include "uhf_uart.h"

static void
    seader_uhf_uart_rx_cb(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* context) {
    if((event & FuriHalSerialRxEventData) == 0) return;

    SeaderUhfUart* uart = context;
    if(!uart || !uart->rx_active) return;

    uint8_t byte = furi_hal_serial_async_rx(handle);
    if(uart->rx.stream_len < sizeof(uart->rx.stream)) {
        uart->rx.stream[uart->rx.stream_len++] = byte;
    } else {
        memmove(uart->rx.stream, uart->rx.stream + 1U, sizeof(uart->rx.stream) - 1U);
        uart->rx.stream[sizeof(uart->rx.stream) - 1U] = byte;
        uart->rx.stream_len = sizeof(uart->rx.stream);
    }
}

bool seader_uhf_uart_open(SeaderUhfUart* uart, FuriHalSerialId serial_id, uint32_t baudrate) {
    if(!uart || uart->handle) return false;
    if(furi_hal_serial_control_is_busy(serial_id)) return false;

    FuriHalSerialHandle* handle = furi_hal_serial_control_acquire(serial_id);
    if(!handle) return false;

    bool initialized_by_app = false;
    if(serial_id == FuriHalSerialIdUsart) {
        initialized_by_app = !furi_hal_bus_is_enabled(FuriHalBusUSART1);
    } else if(serial_id == FuriHalSerialIdLpuart) {
        initialized_by_app = !furi_hal_bus_is_enabled(FuriHalBusLPUART1);
    }

    if(initialized_by_app) {
        furi_hal_serial_init(handle, baudrate);
    } else {
        furi_hal_serial_set_br(handle, baudrate);
    }

    memset(uart, 0, sizeof(*uart));
    uart->handle = handle;
    uart->initialized_by_app = initialized_by_app;
    uart->rx_active = true;
    furi_hal_serial_async_rx_start(handle, seader_uhf_uart_rx_cb, uart, false);
    return true;
}

void seader_uhf_uart_close(SeaderUhfUart* uart) {
    if(!uart || !uart->handle) return;

    uart->rx_active = false;
    furi_hal_serial_async_rx_stop(uart->handle);
    if(uart->initialized_by_app) {
        furi_hal_serial_deinit(uart->handle);
    }
    furi_hal_serial_control_release(uart->handle);
    memset(uart, 0, sizeof(*uart));
}

bool seader_uhf_uart_set_baudrate(SeaderUhfUart* uart, uint32_t baudrate) {
    if(!uart || !uart->handle) return false;
    furi_hal_serial_set_br(uart->handle, baudrate);
    return true;
}

bool seader_uhf_uart_tx(SeaderUhfUart* uart, const uint8_t* data, size_t len) {
    if(!uart || !uart->handle || !data || len == 0U) return false;
    furi_hal_serial_tx(uart->handle, data, len);
    furi_hal_serial_tx_wait_complete(uart->handle);
    return true;
}

void seader_uhf_uart_rx_reset(SeaderUhfUart* uart) {
    if(!uart) return;
    memset(&uart->rx, 0, sizeof(uart->rx));
}
