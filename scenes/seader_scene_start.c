#include "../seader_i.h"
#include "seader_scene_read_common.h"

enum SubmenuIndex {
    SubmenuIndexSamPresent,
    SubmenuIndexSamMissing,
};

static bool seader_scene_start_sam_probe_complete(const Seader* seader) {
    return seader && seader->sam_present && seader->uhf_sam_support != SeaderUhfSamSupportUnknown;
}

void seader_scene_start_submenu_callback(void* context, uint32_t index) {
    Seader* seader = context;
    view_dispatcher_send_custom_event(seader->view_dispatcher, index);
}

void seader_scene_start_on_enter(void* context) {
    Seader* seader = context;

    Popup* popup = seader->popup;

    popup_set_header(popup, "Detecting SAM", 58, 48, AlignCenter, AlignCenter);

    // Start worker
    seader_worker_start(
        seader->worker,
        SeaderWorkerStateCheckSam,
        seader->uart,
        seader_sam_check_worker_callback,
        seader);
    view_dispatcher_switch_to_view(seader->view_dispatcher, SeaderViewPopup);
}

bool seader_scene_start_on_event(void* context, SceneManagerEvent event) {
    Seader* seader = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SeaderWorkerEventSamPresent) {
            seader->sam_present = true;
            seader->worker->sam_present = true;
            seader->uhf_sam_support = SeaderUhfSamSupportUnknown;
            seader->worker->uhf_sam_support = SeaderUhfSamSupportUnknown;
            popup_set_header(
                seader->popup, "Checking SAM\nUHF support", 58, 40, AlignCenter, AlignCenter);
            if(seader_scene_start_sam_probe_complete(seader)) {
                scene_manager_next_scene(seader->scene_manager, SeaderSceneSamPresent);
            }
            consumed = true;
        } else if(event.event == SeaderWorkerEventSamMissing) {
            seader->sam_present = false;
            seader->worker->sam_present = false;
            seader->sam_is_ice1803 = false;
            seader->uhf_sam_support = SeaderUhfSamSupportUnavailable;
            seader->worker->uhf_sam_support = SeaderUhfSamSupportUnavailable;
            scene_manager_next_scene(seader->scene_manager, SeaderSceneSamMissing);
            consumed = true;
        } else if(event.event == SeaderWorkerEventSamWrong) {
            seader->sam_present = false;
            seader->worker->sam_present = false;
            seader->sam_is_ice1803 = false;
            scene_manager_next_scene(seader->scene_manager, SeaderSceneSamWrong);
            consumed = true;
        } else if(event.event == SeaderCustomEventSamStatusUpdated) {
            if(seader_scene_start_sam_probe_complete(seader)) {
                scene_manager_next_scene(seader->scene_manager, SeaderSceneSamPresent);
            }
            consumed = true;
        }

        scene_manager_set_scene_state(seader->scene_manager, SeaderSceneStart, event.event);
    }

    return consumed;
}

void seader_scene_start_on_exit(void* context) {
    Seader* seader = context;
    popup_reset(seader->popup);
}
