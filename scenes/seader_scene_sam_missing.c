#include "../seader_i.h"
enum SubmenuIndex {
    SubmenuIndexDetectSam,
    SubmenuIndexReadUhf,
    SubmenuIndexSaved,
    SubmenuIndexNoUhfHwSam,
};

void seader_scene_sam_missing_submenu_callback(void* context, uint32_t index) {
    Seader* seader = context;
    view_dispatcher_send_custom_event(seader->view_dispatcher, index);
}

void seader_scene_sam_missing_on_enter(void* context) {
    Seader* seader = context;

    if(seader->uhf) {
        seader->worker->uhf_module_present = seader_uhf_is_available(seader->uhf);
    }

    Submenu* submenu = seader->submenu;
    submenu_reset(submenu);

    submenu_add_item(
        submenu,
        "No SAM: Retry",
        SubmenuIndexDetectSam,
        seader_scene_sam_missing_submenu_callback,
        seader);
    if(seader->uhf && seader_uhf_is_available(seader->uhf)) {
        submenu_add_item(
            submenu,
            "Read UHF",
            SubmenuIndexReadUhf,
            seader_scene_sam_missing_submenu_callback,
            seader);
    } else {
        submenu_add_item(
            submenu,
            "No UHF HW/SAM",
            SubmenuIndexNoUhfHwSam,
            seader_scene_sam_missing_submenu_callback,
            seader);
    }
    submenu_add_item(
        submenu, "Saved", SubmenuIndexSaved, seader_scene_sam_missing_submenu_callback, seader);

    submenu_set_selected_item(
        submenu, scene_manager_get_scene_state(seader->scene_manager, SeaderSceneSamPresent));

    view_dispatcher_switch_to_view(seader->view_dispatcher, SeaderViewMenu);
}

bool seader_scene_sam_missing_on_event(void* context, SceneManagerEvent event) {
    Seader* seader = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SeaderCustomEventSamStatusUpdated) {
            seader_scene_sam_missing_on_enter(context);
            return true;
        }
        if(event.event == SubmenuIndexDetectSam) {
            scene_manager_next_scene(seader->scene_manager, SeaderSceneStart);
            consumed = true;
        } else if(event.event == SubmenuIndexReadUhf) {
            scene_manager_next_scene(seader->scene_manager, SeaderSceneReadUhfMenu);
            consumed = true;
        } else if(event.event == SubmenuIndexNoUhfHwSam) {
            consumed = true;
        } else if(event.event == SubmenuIndexSaved) {
            scene_manager_next_scene(seader->scene_manager, SeaderSceneFileSelect);
            consumed = true;
        } else if(event.event == SeaderWorkerEventSamPresent) {
            seader->sam_present = true;
            seader->worker->sam_present = true;
            seader->uhf_sam_support = SeaderUhfSamSupportUnknown;
            seader->worker->uhf_sam_support = SeaderUhfSamSupportUnknown;
            scene_manager_next_scene(seader->scene_manager, SeaderSceneStart);
            consumed = true;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        scene_manager_stop(seader->scene_manager);
        view_dispatcher_stop(seader->view_dispatcher);
        consumed = true;
    }

    return consumed;
}

void seader_scene_sam_missing_on_exit(void* context) {
    Seader* seader = context;
    submenu_reset(seader->submenu);
}
