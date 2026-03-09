#pragma once

#include <furi.h>
#include <furi_hal.h>

#define SEADER_UHF_UART_RX_STREAM_MAX 384U

typedef struct {
    uint8_t stream[SEADER_UHF_UART_RX_STREAM_MAX];
    size_t stream_len;
} SeaderUhfUartRx;

typedef struct {
    FuriHalSerialHandle* handle;
    bool initialized_by_app;
    bool rx_active;
    SeaderUhfUartRx rx;
} SeaderUhfUart;

bool seader_uhf_uart_open(SeaderUhfUart* uart, FuriHalSerialId serial_id, uint32_t baudrate);
void seader_uhf_uart_close(SeaderUhfUart* uart);
bool seader_uhf_uart_set_baudrate(SeaderUhfUart* uart, uint32_t baudrate);
bool seader_uhf_uart_tx(SeaderUhfUart* uart, const uint8_t* data, size_t len);
void seader_uhf_uart_rx_reset(SeaderUhfUart* uart);
