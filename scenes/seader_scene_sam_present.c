#include "../seader_i.h"
enum SubmenuIndex {
    SubmenuIndexReadHF,
    SubmenuIndexReadUHF,
    SubmenuIndexSaved,
    SubmenuIndexIce1803Sam,
    SubmenuIndexNoUhfHwSam,
    SubmenuIndexAPDURunner,
    SubmenuIndexSamInfo,
    SubmenuIndexFwVersion,
    SubmenuIndexReadConfigCard,
};

static uint8_t fwChecks = 3;

static bool seader_scene_sam_present_should_show_read_uhf(const Seader* seader) {
    if(!seader || !seader->worker) return false;
    return seader_uhf_should_show_root_menu(
        seader->uhf && seader_uhf_is_available(seader->uhf),
        (SeaderUhfSamSupport)seader->worker->uhf_sam_support);
}

static bool seader_scene_sam_present_should_show_no_uhf_hw_sam(const Seader* seader) {
    if(!seader || !seader->worker) return false;
    return seader_uhf_should_show_root_unavailable(
        seader->uhf && seader_uhf_is_available(seader->uhf),
        (SeaderUhfSamSupport)seader->worker->uhf_sam_support);
}

void seader_scene_sam_present_submenu_callback(void* context, uint32_t index) {
    Seader* seader = context;
    view_dispatcher_send_custom_event(seader->view_dispatcher, index);
}

void seader_scene_sam_present_on_update(void* context) {
    Seader* seader = context;
    SeaderWorker* seader_worker = seader->worker;

    Submenu* submenu = seader->submenu;
    submenu_reset(submenu);

    submenu_add_item(
        submenu, "Read HF", SubmenuIndexReadHF, seader_scene_sam_present_submenu_callback, seader);
    if(seader_scene_sam_present_should_show_read_uhf(seader)) {
        submenu_add_item(
            submenu,
            "Read UHF",
            SubmenuIndexReadUHF,
            seader_scene_sam_present_submenu_callback,
            seader);
    }
    submenu_add_item(
        submenu, "Saved", SubmenuIndexSaved, seader_scene_sam_present_submenu_callback, seader);
    if(seader->sam_is_ice1803) {
        submenu_add_item(
            submenu,
            "ICE1803 SAM",
            SubmenuIndexIce1803Sam,
            seader_scene_sam_present_submenu_callback,
            seader);
    }
    if(seader_scene_sam_present_should_show_no_uhf_hw_sam(seader)) {
        submenu_add_item(
            submenu,
            "No UHF HW/SAM",
            SubmenuIndexNoUhfHwSam,
            seader_scene_sam_present_submenu_callback,
            seader);
    }

    if(seader->is_debug_enabled) {
        submenu_add_item(
            submenu,
            "Read Config Card",
            SubmenuIndexReadConfigCard,
            seader_scene_sam_present_submenu_callback,
            seader);
    }

    if(apdu_log_check_presence(SEADER_APDU_RUNNER_FILE_NAME)) {
        submenu_add_item(
            submenu,
            "Run APDUs",
            SubmenuIndexAPDURunner,
            seader_scene_sam_present_submenu_callback,
            seader);
    }
    if(seader_worker->sam_version[0] != 0 && seader_worker->sam_version[1] != 0) {
        // Use reusable string instead of allocating new one
        FuriString* fw_str = seader->temp_string1;
        furi_string_reset(fw_str);
        furi_string_cat_printf(
            fw_str, "FW %d.%d", seader_worker->sam_version[0], seader_worker->sam_version[1]);
        submenu_add_item(
            submenu,
            furi_string_get_cstr(fw_str),
            SubmenuIndexFwVersion,
            seader_scene_sam_present_submenu_callback,
            seader);
        // No need to free fw_str as it's reused from seader struct
        fwChecks = 0;
    }

    submenu_set_selected_item(
        submenu, scene_manager_get_scene_state(seader->scene_manager, SeaderSceneSamPresent));

    view_dispatcher_switch_to_view(seader->view_dispatcher, SeaderViewMenu);
}

void seader_scene_sam_present_on_enter(void* context) {
    Seader* seader = context;
    if(seader->uhf) {
        seader->worker->uhf_module_present = seader_uhf_is_available(seader->uhf);
    }
    seader_scene_sam_present_on_update(context);
}

bool seader_scene_sam_present_on_event(void* context, SceneManagerEvent event) {
    Seader* seader = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SeaderCustomEventSamStatusUpdated) {
            seader_scene_sam_present_on_update(context);
            return true;
        }
        scene_manager_set_scene_state(seader->scene_manager, SeaderSceneSamPresent, event.event);

        if(event.event == SubmenuIndexReadHF) {
            seader->read_scope = SeaderReadScopeHF;
            seader->selected_read_type = SeaderCredentialTypeNone;
            seader->uhf_read_mode = SeaderUhfReadModeNone;
            scene_manager_next_scene(seader->scene_manager, SeaderSceneRead);
            consumed = true;
        } else if(event.event == SubmenuIndexReadUHF) {
            if(seader_uhf_root_leads_to_no_module(
                   seader->uhf && seader_uhf_is_available(seader->uhf),
                   seader->uhf_sam_support)) {
                scene_manager_next_scene(seader->scene_manager, SeaderSceneReadUhfNoModule);
            } else {
                scene_manager_next_scene(seader->scene_manager, SeaderSceneReadUhfMenu);
            }
            consumed = true;
        } else if(event.event == SubmenuIndexIce1803Sam) {
            scene_manager_next_scene(seader->scene_manager, SeaderSceneSamInfo);
            consumed = true;
        } else if(event.event == SubmenuIndexNoUhfHwSam) {
            consumed = true;
        } else if(event.event == SubmenuIndexReadConfigCard) {
            scene_manager_set_scene_state(
                seader->scene_manager, SeaderSceneSamPresent, SubmenuIndexReadConfigCard);
            scene_manager_next_scene(seader->scene_manager, SeaderSceneReadConfigCard);
            consumed = true;
        } else if(event.event == SubmenuIndexSamInfo) {
            scene_manager_next_scene(seader->scene_manager, SeaderSceneSamInfo);
            consumed = true;
        } else if(event.event == SubmenuIndexSaved) {
            scene_manager_next_scene(seader->scene_manager, SeaderSceneFileSelect);
            consumed = true;
        } else if(event.event == SubmenuIndexFwVersion) {
            consumed = true;
        } else if(event.event == SeaderWorkerEventSamMissing) {
            scene_manager_next_scene(seader->scene_manager, SeaderSceneSamMissing);
            consumed = true;
        } else if(event.event == SubmenuIndexAPDURunner) {
            scene_manager_next_scene(seader->scene_manager, SeaderSceneAPDURunner);
            consumed = true;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        seader_clear_sam_card_if_announced(seader);
        scene_manager_stop(seader->scene_manager);
        view_dispatcher_stop(seader->view_dispatcher);
        consumed = true;
    } else if(event.type == SceneManagerEventTypeTick) {
        SeaderWorker* seader_worker = seader->worker;
        if(fwChecks > 0 && seader_worker->sam_version[0] != 0 &&
           seader_worker->sam_version[1] != 0) {
            fwChecks--;
            seader_scene_sam_present_on_update(context);
        }
    }

    return consumed;
}

void seader_scene_sam_present_on_exit(void* context) {
    Seader* seader = context;
    submenu_reset(seader->submenu);
}
