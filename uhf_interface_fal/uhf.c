#ifndef ASN_EMIT_DEBUG
#define ASN_EMIT_DEBUG 0
#endif

#include "uhf_interface.h"

#include "../uhf_module.h"
#include <flipper_application/flipper_application.h>

#define TAG "PluginUHF"

typedef struct {
    const PluginUhfHostApi* api;
    void* host_ctx;
    SeaderUhf* uhf;
    bool started;
} PluginUhfContext;

static bool plugin_uhf_validate_host_api(const PluginUhfHostApi* api) {
    if(!api) {
        return false;
    }

#define UHF_REQUIRE_API(field) \
    do {                       \
        if(!(api->field)) {    \
            return false;      \
        }                      \
    } while(false)

    UHF_REQUIRE_API(sam_can_accept_card);
    UHF_REQUIRE_API(send_card_detected);
    UHF_REQUIRE_API(set_credential_type);
    UHF_REQUIRE_API(set_pacs_media_type);

#undef UHF_REQUIRE_API
    return true;
}

static PluginUhfContext* plugin_uhf_require_ctx(void* plugin_ctx) {
    PluginUhfContext* ctx = plugin_ctx;
    furi_check(ctx);
    furi_check(ctx->api);
    furi_check(ctx->host_ctx);
    furi_check(ctx->uhf);
    return ctx;
}

static PluginUhfStartReadResult plugin_uhf_start_result(
    PluginUhfStartReadOutcome outcome,
    PluginUhfReadFailureReason failure_reason) {
    PluginUhfStartReadResult result = {
        .outcome = outcome,
        .failure_reason = failure_reason,
    };
    return result;
}

static void* plugin_uhf_alloc(const PluginUhfHostApi* api, void* host_ctx) {
    if(!plugin_uhf_validate_host_api(api) || !host_ctx) {
        return NULL;
    }

    PluginUhfContext* ctx = calloc(1, sizeof(PluginUhfContext));
    if(!ctx) {
        return NULL;
    }

    ctx->uhf = seader_uhf_alloc();
    if(!ctx->uhf) {
        free(ctx);
        return NULL;
    }

    ctx->api = api;
    ctx->host_ctx = host_ctx;
    return ctx;
}

static void plugin_uhf_free(void* plugin_ctx) {
    PluginUhfContext* ctx = plugin_ctx;
    if(!ctx) {
        return;
    }

    if(ctx->uhf) {
        seader_uhf_free(ctx->uhf);
    }
    free(ctx);
}

static PluginUhfStartReadResult plugin_uhf_start_read(void* plugin_ctx) {
    PluginUhfContext* ctx = plugin_uhf_require_ctx(plugin_ctx);
    SeaderUhfReadResult result = {0};

    ctx->api->set_credential_type(ctx->host_ctx, SeaderCredentialTypeUhf);
    ctx->api->set_pacs_media_type(ctx->host_ctx, SeaderPacsMediaTypeUhf);

    if(!ctx->api->sam_can_accept_card(ctx->host_ctx)) {
        return plugin_uhf_start_result(
            PluginUhfStartReadOutcomeTerminalFail, PluginUhfReadFailureReasonSamBusy);
    }

    if(!ctx->started) {
        if(!seader_uhf_begin(ctx->uhf)) {
            return plugin_uhf_start_result(
                PluginUhfStartReadOutcomeTerminalFail,
                PluginUhfReadFailureReasonModuleUnavailable);
        }
        ctx->started = true;
    } else if(!seader_uhf_is_available(ctx->uhf) && !seader_uhf_begin(ctx->uhf)) {
        return plugin_uhf_start_result(
            PluginUhfStartReadOutcomeTerminalFail, PluginUhfReadFailureReasonModuleUnavailable);
    }

    if(!seader_uhf_inventory_tag(ctx->uhf, &result)) {
        const PluginUhfReadFailureReason reason = result.failure_reason;
        if(reason == PluginUhfReadFailureReasonNoTagSeen ||
           reason == PluginUhfReadFailureReasonSelectFailed) {
            return plugin_uhf_start_result(
                PluginUhfStartReadOutcomeRetry, PluginUhfReadFailureReasonNone);
        }
        if(reason == PluginUhfReadFailureReasonModuleUnavailable) {
            seader_uhf_end(ctx->uhf);
            ctx->started = false;
            if(seader_uhf_begin(ctx->uhf)) {
                ctx->started = true;
                return plugin_uhf_start_result(
                    PluginUhfStartReadOutcomeRetry, PluginUhfReadFailureReasonNone);
            }
        }

        return plugin_uhf_start_result(PluginUhfStartReadOutcomeTerminalFail, reason);
    }

    if(result.tag.sam_csn_len != SEADER_UHF_NORMALIZED_CSN_LEN) {
        return plugin_uhf_start_result(
            PluginUhfStartReadOutcomeTerminalFail, PluginUhfReadFailureReasonUnsupportedShape);
    }

    if(!ctx->api->send_card_detected(ctx->host_ctx, result.tag.sam_csn, result.tag.sam_csn_len)) {
        return plugin_uhf_start_result(
            PluginUhfStartReadOutcomeTerminalFail, PluginUhfReadFailureReasonSamCardDetectFailed);
    }

    return plugin_uhf_start_result(
        PluginUhfStartReadOutcomeDetected, PluginUhfReadFailureReasonNone);
}

static void plugin_uhf_stop(void* plugin_ctx) {
    PluginUhfContext* ctx = plugin_ctx;
    if(!ctx || !ctx->uhf) {
        return;
    }

    seader_uhf_end(ctx->uhf);
    ctx->started = false;
}

static bool plugin_uhf_handle_i2c_command(
    void* plugin_ctx,
    const PluginUhfRequest* request,
    PluginUhfResponse* response) {
    PluginUhfContext* ctx = plugin_uhf_require_ctx(plugin_ctx);
    return seader_uhf_build_i2c_response(ctx->uhf, request, response);
}

static const PluginUhf plugin_uhf = {
    .name = "Plugin UHF",
    .alloc = plugin_uhf_alloc,
    .free = plugin_uhf_free,
    .start_read = plugin_uhf_start_read,
    .stop = plugin_uhf_stop,
    .handle_i2c_command = plugin_uhf_handle_i2c_command,
};

static const FlipperAppPluginDescriptor plugin_uhf_descriptor = {
    .appid = UHF_PLUGIN_APP_ID,
    .ep_api_version = UHF_PLUGIN_API_VERSION,
    .entry_point = &plugin_uhf,
};

const FlipperAppPluginDescriptor* plugin_uhf_ep(void) {
    return &plugin_uhf_descriptor;
}
