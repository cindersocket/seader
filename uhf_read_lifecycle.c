#include "uhf_read_lifecycle.h"

const char* seader_uhf_read_failure_reason_text(PluginUhfReadFailureReason reason) {
    switch(reason) {
    case PluginUhfReadFailureReasonSamBusy:
        return "SAM not idle";
    case PluginUhfReadFailureReasonModuleUnavailable:
        return "UHF unavailable";
    case PluginUhfReadFailureReasonNoTagSeen:
        return "No supported UHF tag";
    case PluginUhfReadFailureReasonSelectFailed:
        return "UHF tag select failed";
    case PluginUhfReadFailureReasonPublicTidReadFailed:
        return "UHF tag detected, identity unreadable";
    case PluginUhfReadFailureReasonUnsupportedShape:
        return "Unsupported UHF shape";
    case PluginUhfReadFailureReasonSamCardDetectFailed:
        return "SAM card detect failed";
    case PluginUhfReadFailureReasonUnlockFailed:
        return "UHF unlock failed";
    case PluginUhfReadFailureReasonPrivateDataReadFailed:
        return "UHF private data read failed";
    case PluginUhfReadFailureReasonRelockFailed:
        return "UHF relock failed";
    case PluginUhfReadFailureReasonInternalState:
        return "UHF read state error";
    case PluginUhfReadFailureReasonNone:
    default:
        return "Read failed";
    }
}
