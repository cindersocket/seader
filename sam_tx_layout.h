#pragma once

#include <stdbool.h>
#include <stddef.h>

#define SEADER_SAM_CCID_TX_HEADROOM 12U
#define SEADER_SAM_T1_TX_HEADROOM   3U
#define SEADER_SAM_APDU_TX_HEADROOM (SEADER_SAM_CCID_TX_HEADROOM + SEADER_SAM_T1_TX_HEADROOM)

size_t seader_sam_apdu_header_len(bool extended);
size_t seader_sam_payload_offset(bool extended);
size_t seader_sam_payload_capacity(size_t uart_buf_size, bool extended);
