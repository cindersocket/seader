#include "sam_tx_layout.h"

size_t seader_sam_apdu_header_len(bool extended) {
    return extended ? 7U : 5U;
}

size_t seader_sam_payload_offset(bool extended) {
    return SEADER_SAM_APDU_TX_HEADROOM + seader_sam_apdu_header_len(extended);
}

size_t seader_sam_payload_capacity(size_t uart_buf_size, bool extended) {
    size_t offset = seader_sam_payload_offset(extended);
    if(offset >= uart_buf_size) {
        return 0U;
    }

    return uart_buf_size - offset;
}
