#pragma once

#include <nfc/helpers/iso13239_crc.h>
#include <optimized_ikeys.h>
#include <optimized_cipher.h>

#include "seader_i.h"
#include "seader_credential.h"
#include "seader_bridge.h"
#include "seader_worker.h"
#include "protocol/rfal_picopass.h"

#include <Payload.h>
#include <SIO.h>

#define ExternalApplicationA 0x44
#define NFCInterface         0x14
#define SAMInterface         0x0a

void seader_send_payload(
    Seader* seader,
    Payload_t* payload,
    uint8_t from,
    uint8_t to,
    uint8_t replyTo);
bool seader_send_uhf_card_detected(Seader* seader, const uint8_t* epc, size_t epc_len);

NfcCommand seader_worker_card_detect(
    Seader* seader,
    uint8_t sak,
    uint8_t* atqa,
    const uint8_t* uid,
    uint8_t uid_len,
    uint8_t* ats,
    uint8_t ats_len);

void seader_send_no_card_detected(Seader* seader);
void seader_clear_sam_card_if_announced(Seader* seader);
bool seader_sam_can_accept_card(const Seader* seader);
bool seader_sam_has_active_card(const Seader* seader);

bool seader_process_success_response_i(
    Seader* seader,
    uint8_t* apdu,
    size_t len,
    bool online,
    SeaderPollerContainer* spc);
