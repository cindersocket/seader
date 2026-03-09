#include "../seader_i.h"

enum SubmenuIndex {
    SubmenuIndexSio,
    SubmenuIndexEpc,
    SubmenuIndexTid,
};

static void seader_scene_read_uhf_menu_start_mode(Seader* seader, SeaderUhfReadMode mode) {
    furi_assert(seader);

    seader->read_scope = SeaderReadScopeUHF;
    seader->selected_read_type = SeaderCredentialTypeUhf;
    seader->uhf_read_mode = mode;
    scene_manager_next_scene(seader->scene_manager, SeaderSceneRead);
}

static const char* seader_scene_read_uhf_menu_sio_label(const Seader* seader) {
    return seader_uhf_sio_menu_label(seader->sam_present, seader->uhf_sam_support);
}

static bool seader_scene_read_uhf_menu_sio_enabled(const Seader* seader) {
    return seader_uhf_sio_menu_enabled(seader->sam_present, seader->uhf_sam_support);
}

static void seader_scene_read_uhf_menu_submenu_callback(void* context, uint32_t index) {
    Seader* seader = context;
    view_dispatcher_send_custom_event(seader->view_dispatcher, index);
}

void seader_scene_read_uhf_menu_on_enter(void* context) {
    Seader* seader = context;
    Submenu* submenu = seader->submenu;

    if(seader->uhf) {
        (void)seader_uhf_begin(seader->uhf);
        seader->worker->uhf_module_present = seader_uhf_is_available(seader->uhf);
    }

    submenu_reset(submenu);
    submenu_add_item(
        submenu,
        seader_scene_read_uhf_menu_sio_label(seader),
        SubmenuIndexSio,
        seader_scene_read_uhf_menu_submenu_callback,
        seader);
    submenu_add_item(
        submenu, "Read EPC", SubmenuIndexEpc, seader_scene_read_uhf_menu_submenu_callback, seader);
    submenu_add_item(
        submenu, "Read TID", SubmenuIndexTid, seader_scene_read_uhf_menu_submenu_callback, seader);

    submenu_set_selected_item(
        submenu, scene_manager_get_scene_state(seader->scene_manager, SeaderSceneReadUhfMenu));
    view_dispatcher_switch_to_view(seader->view_dispatcher, SeaderViewMenu);
}

bool seader_scene_read_uhf_menu_on_event(void* context, SceneManagerEvent event) {
    Seader* seader = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        scene_manager_set_scene_state(seader->scene_manager, SeaderSceneReadUhfMenu, event.event);

        if(event.event == SubmenuIndexSio) {
            consumed = true;
            if(seader_scene_read_uhf_menu_sio_enabled(seader)) {
                seader_scene_read_uhf_menu_start_mode(seader, SeaderUhfReadModeSio);
            }
        } else if(event.event == SubmenuIndexEpc) {
            seader_scene_read_uhf_menu_start_mode(seader, SeaderUhfReadModeEpc);
            consumed = true;
        } else if(event.event == SubmenuIndexTid) {
            seader_scene_read_uhf_menu_start_mode(seader, SeaderUhfReadModeTid);
            consumed = true;
        } else if(event.event == SeaderCustomEventSamStatusUpdated) {
            seader_scene_read_uhf_menu_on_enter(seader);
            consumed = true;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        seader->selected_read_type = SeaderCredentialTypeNone;
        seader->read_scope = SeaderReadScopeAll;
        seader->uhf_read_mode = SeaderUhfReadModeNone;
        if(seader->uhf) {
            seader->worker->uhf_module_present = seader_uhf_is_available(seader->uhf);
        }
        consumed = scene_manager_previous_scene(seader->scene_manager);
    }

    return consumed;
}

void seader_scene_read_uhf_menu_on_exit(void* context) {
    Seader* seader = context;
    submenu_reset(seader->submenu);
}
