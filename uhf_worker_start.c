#include "uhf_worker_start.h"

bool seader_uhf_worker_start_can_continue(
    bool acquire_ok,
    bool plugin_present,
    bool plugin_ctx_present) {
    return acquire_ok && plugin_present && plugin_ctx_present;
}
