#include "../seader_i.h"
enum SubmenuIndex {
    SubmenuIndexRead,
    SubmenuIndexSaved,
    SubmenuIndexAPDURunner,
    SubmenuIndexSamInfo,
    SubmenuIndexUhfStatus,
};

static uint8_t fwChecks = 3;

void seader_scene_sam_present_submenu_callback(void* context, uint32_t index);

static void seader_scene_sam_present_add_item(
    Submenu* submenu,
    const char* label,
    uint32_t event_id,
    uint32_t requested_event_id,
    uint32_t* item_position,
    uint32_t* selected_position,
    bool* selected_event_matched,
    Seader* seader) {
    furi_check(submenu);
    furi_check(label);
    furi_check(item_position);
    furi_check(selected_position);
    furi_check(selected_event_matched);
    furi_check(seader);

    if(event_id == requested_event_id) {
        *selected_position = *item_position;
        *selected_event_matched = true;
    }
    submenu_add_item(submenu, label, event_id, seader_scene_sam_present_submenu_callback, seader);
    (*item_position)++;
}

static void seader_scene_sam_present_rebuild_menu(Seader* seader, uint32_t selected_item) {
    furi_check(seader);
    furi_check(seader->submenu);
    furi_check(seader->view_dispatcher);

    Submenu* submenu = seader->submenu;
    submenu_reset(submenu);

    uint32_t item_position = 0U;
    uint32_t selected_position = 0U;
    bool selected_event_matched = false;
    seader_scene_sam_present_add_item(
        submenu,
        "Read HF",
        SubmenuIndexRead,
        selected_item,
        &item_position,
        &selected_position,
        &selected_event_matched,
        seader);
    seader_scene_sam_present_add_item(
        submenu,
        "Saved",
        SubmenuIndexSaved,
        selected_item,
        &item_position,
        &selected_position,
        &selected_event_matched,
        seader);

    if(apdu_log_check_presence(SEADER_APDU_RUNNER_FILE_NAME)) {
        seader_scene_sam_present_add_item(
            submenu,
            "Run APDUs",
            SubmenuIndexAPDURunner,
            selected_item,
            &item_position,
            &selected_position,
            &selected_event_matched,
            seader);
    }
    seader_scene_sam_present_add_item(
        submenu,
        seader->sam_key_label,
        SubmenuIndexSamInfo,
        selected_item,
        &item_position,
        &selected_position,
        &selected_event_matched,
        seader);
    if(seader->uhf_module_label[0] != '\0') {
        seader_scene_sam_present_add_item(
            submenu,
            seader->uhf_module_label,
            SubmenuIndexUhfStatus,
            selected_item,
            &item_position,
            &selected_position,
            &selected_event_matched,
            seader);
    }

    if(seader->sam_version[0] != 0 && seader->sam_version[1] != 0) {
        fwChecks = 0;
    }

    if(!selected_event_matched && item_position > 0U && selected_item < item_position) {
        selected_position = selected_item;
    }
    submenu_set_selected_item(submenu, selected_position);
    view_dispatcher_switch_to_view(seader->view_dispatcher, SeaderViewMenu);
}

void seader_scene_sam_present_submenu_callback(void* context, uint32_t index) {
    Seader* seader = context;
    furi_check(seader);
    furi_check(seader->view_dispatcher);

    view_dispatcher_send_custom_event(seader->view_dispatcher, index);
}

void seader_scene_sam_present_on_update(void* context) {
    Seader* seader = context;
    seader_scene_sam_present_rebuild_menu(
        seader, scene_manager_get_scene_state(seader->scene_manager, SeaderSceneSamPresent));
}

void seader_scene_sam_present_on_enter(void* context) {
    seader_scene_sam_present_on_update(context);
}

bool seader_scene_sam_present_on_event(void* context, SceneManagerEvent event) {
    Seader* seader = context;
    furi_check(seader);

    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(seader->sam_present_menu_guard_active &&
           (event.event == SubmenuIndexRead || event.event == SubmenuIndexSaved ||
            event.event == SubmenuIndexAPDURunner || event.event == SubmenuIndexSamInfo ||
            event.event == SubmenuIndexUhfStatus)) {
            seader->sam_present_menu_guard_active = false;
            consumed = true;
        } else if(event.event == SubmenuIndexRead) {
            scene_manager_set_scene_state(
                seader->scene_manager, SeaderSceneSamPresent, event.event);
            scene_manager_next_scene(seader->scene_manager, SeaderSceneRead);
            consumed = true;
        } else if(event.event == SubmenuIndexSamInfo) {
            scene_manager_set_scene_state(
                seader->scene_manager, SeaderSceneSamPresent, event.event);
            scene_manager_next_scene(seader->scene_manager, SeaderSceneSamInfo);
            consumed = true;
        } else if(event.event == SubmenuIndexSaved) {
            scene_manager_set_scene_state(
                seader->scene_manager, SeaderSceneSamPresent, event.event);
            scene_manager_next_scene(seader->scene_manager, SeaderSceneFileSelect);
            consumed = true;
        } else if(event.event == SeaderWorkerEventSamMissing) {
            if(seader->board_attachment == SeaderBoardAttachmentUhfCarrier) {
                seader->board_status = SeaderBoardStatusReady;
                seader->sam_present = false;
                seader_sam_key_label_format(
                    false,
                    SeaderSamKeyProbeStatusUnknown,
                    NULL,
                    0U,
                    seader->sam_key_label,
                    sizeof(seader->sam_key_label));
                seader_scene_sam_present_rebuild_menu(seader, SubmenuIndexSamInfo);
            } else {
                seader->board_status = seader_board_status_on_sam_missing(seader->board_status);
                scene_manager_next_scene(seader->scene_manager, SeaderSceneSamMissing);
            }
            consumed = true;
        } else if(event.event == SubmenuIndexAPDURunner) {
            scene_manager_set_scene_state(
                seader->scene_manager, SeaderSceneSamPresent, event.event);
            scene_manager_next_scene(seader->scene_manager, SeaderSceneAPDURunner);
            consumed = true;
        } else if(event.event == SubmenuIndexUhfStatus) {
            scene_manager_set_scene_state(
                seader->scene_manager, SeaderSceneSamPresent, event.event);
            scene_manager_next_scene(seader->scene_manager, SeaderSceneUhfInfo);
            consumed = true;
        } else if(event.event == SeaderWorkerEventHfTeardownComplete) {
            consumed = seader_hf_finish_teardown_action(seader);
        } else if(event.event == SeaderCustomEventSamStatusUpdated) {
            seader_scene_sam_present_rebuild_menu(
                seader, submenu_get_selected_item(seader->submenu));
            consumed = true;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        consumed = seader_hf_request_teardown(seader, SeaderHfTeardownActionStopApp);
    } else if(event.type == SceneManagerEventTypeTick) {
        if(seader->sam_present_menu_guard_active) {
            seader->sam_present_menu_guard_active = false;
        }
        if(fwChecks > 0 && seader->sam_version[0] != 0 && seader->sam_version[1] != 0) {
            fwChecks--;
            seader_scene_sam_present_rebuild_menu(
                seader, submenu_get_selected_item(seader->submenu));
        }
    }

    return consumed;
}

void seader_scene_sam_present_on_exit(void* context) {
    Seader* seader = context;
    if(seader && seader->submenu) {
        submenu_reset(seader->submenu);
    }
}
