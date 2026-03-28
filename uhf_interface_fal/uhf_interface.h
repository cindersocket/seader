#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../seader_credential_type.h"
#include "../seader_pacs_media_type.h"
#include <flipper_application/flipper_application.h>

#define UHF_PLUGIN_APP_ID      "plugin_uhf"
#define UHF_PLUGIN_API_VERSION 1

typedef enum {
    PluginUhfCommandNone = 0,
    PluginUhfCommandGetVersion,
    PluginUhfCommandGetProperties,
    PluginUhfCommandSetAccessPassword,
    PluginUhfCommandGetPrivateData,
} PluginUhfCommand;

typedef struct {
    uint32_t bus_address;
    PluginUhfCommand command;
    uint8_t command_value[32];
    size_t command_value_len;
} PluginUhfRequest;

typedef enum {
    PluginUhfResponseNone = 0,
    PluginUhfResponseVersion,
    PluginUhfResponseGetProperties,
    PluginUhfResponseSetAccessPassword,
    PluginUhfResponseGetPrivateData,
} PluginUhfResponseType;

typedef enum {
    PluginUhfStartReadOutcomeRetry = 0,
    PluginUhfStartReadOutcomeDetected,
    PluginUhfStartReadOutcomeTerminalFail,
} PluginUhfStartReadOutcome;

typedef enum {
    PluginUhfReadFailureReasonNone = 0,
    PluginUhfReadFailureReasonSamBusy,
    PluginUhfReadFailureReasonModuleUnavailable,
    PluginUhfReadFailureReasonNoTagSeen,
    PluginUhfReadFailureReasonSelectFailed,
    PluginUhfReadFailureReasonPublicTidReadFailed,
    PluginUhfReadFailureReasonUnsupportedShape,
    PluginUhfReadFailureReasonSamCardDetectFailed,
    PluginUhfReadFailureReasonUnlockFailed,
    PluginUhfReadFailureReasonPrivateDataReadFailed,
    PluginUhfReadFailureReasonRelockFailed,
    PluginUhfReadFailureReasonInternalState,
} PluginUhfReadFailureReason;

typedef enum {
    PluginUhfDebugStageNone = 0,
    PluginUhfDebugStagePdApplyProfile = 0x0101,
    PluginUhfDebugStagePdPublicSelectEpc = 0x0110,
    PluginUhfDebugStagePdPublicUnlockQt = 0x0111,
    PluginUhfDebugStagePdPublicDeriveTid = 0x0112,
    PluginUhfDebugStagePdPublicRefreshAfterUnlock = 0x0113,
    PluginUhfDebugStagePdPublicSelectTid = 0x0114,
    PluginUhfDebugStagePdPublicWaitSelected = 0x0115,
    PluginUhfDebugStagePdPublicReadUser = 0x0116,
    PluginUhfDebugStagePdPrivateReadTid = 0x0120,
    PluginUhfDebugStagePdPrivateParseTid = 0x0121,
    PluginUhfDebugStagePdPrivateSelectTid = 0x0122,
    PluginUhfDebugStagePdPrivateReadUser = 0x0123,
    PluginUhfDebugStagePdRelock = 0x0130,
    PluginUhfDebugStagePdNormalize = 0x0140,
} PluginUhfDebugStage;

typedef struct {
    PluginUhfStartReadOutcome outcome;
    PluginUhfReadFailureReason failure_reason;
} PluginUhfStartReadResult;

typedef struct {
    PluginUhfResponseType type;
    PluginUhfReadFailureReason failure_reason;
    uint16_t debug_stage;
    uint8_t version[48];
    size_t version_len;
    uint8_t tid[12];
    size_t tid_len;
    uint8_t user_data[64];
    size_t user_data_len;
} PluginUhfResponse;

typedef struct {
    bool (*sam_can_accept_card)(void* host_ctx);
    bool (*send_card_detected)(void* host_ctx, const uint8_t* sam_csn, size_t sam_csn_len);
    void (*set_credential_type)(void* host_ctx, SeaderCredentialType type);
    void (*set_pacs_media_type)(void* host_ctx, SeaderPacsMediaType type);
} PluginUhfHostApi;

typedef struct {
    const char* name;
    void* (*alloc)(const PluginUhfHostApi* api, void* host_ctx);
    void (*free)(void* plugin_ctx);
    PluginUhfStartReadResult (*start_read)(void* plugin_ctx);
    void (*stop)(void* plugin_ctx);
    bool (*handle_i2c_command)(
        void* plugin_ctx,
        const PluginUhfRequest* request,
        PluginUhfResponse* response);
} PluginUhf;

const FlipperAppPluginDescriptor* plugin_uhf_ep(void);
