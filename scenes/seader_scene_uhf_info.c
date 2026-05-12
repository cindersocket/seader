#include "../seader_i.h"

#define TAG "SeaderUhfInfoScene"

static void seader_scene_uhf_info_widget_callback(
    GuiButtonType result,
    InputType type,
    void* context) {
    Seader* seader = context;
    if(type == InputTypeShort) {
        furi_check(seader);
        furi_check(seader->view_dispatcher);
        view_dispatcher_send_custom_event(seader->view_dispatcher, result);
    }
}

static void seader_scene_uhf_info_format_body(Seader* seader, FuriString* body) {
    furi_check(seader);
    furi_check(body);

    furi_string_reset(body);
    if(!seader->sam_present) {
        furi_string_set_str(body, "SAM: not detected\nKeys unavailable");
        return;
    }

    switch(seader->uhf_probe_status) {
    case SeaderUhfProbeStatusUnknown:
        furi_string_set_str(body, "SAM keyset:\nprobing...");
        return;
    case SeaderUhfProbeStatusFailed:
        furi_string_set_str(body, "SAM keyset:\nprobe failed");
        return;
    case SeaderUhfProbeStatusSuccess:
        break;
    }

    if(!seader->snmp_probe.has_monza4qt && !seader->snmp_probe.has_higgs3) {
        furi_string_set_str(body, "SAM keyset:\nnone");
        return;
    }

    if(seader->snmp_probe.has_monza4qt) {
        furi_string_cat_printf(
            body,
            "Monza 4QT: %s",
            seader->snmp_probe.monza4qt_key_present ? "present" : "no key");
    }

    if(seader->snmp_probe.has_higgs3) {
        if(furi_string_size(body) > 0U) {
            furi_string_push_back(body, '\n');
        }
        furi_string_cat_printf(
            body,
            "Higgs 3: %s",
            seader->snmp_probe.higgs3_key_present ? "present" : "no key");
    }
}

void seader_scene_uhf_info_on_enter(void* context) {
    Seader* seader = context;
    furi_check(seader);

    Widget* widget = seader_get_widget(seader);
    if(!widget) {
        FURI_LOG_E(TAG, "Widget view unavailable");
        return;
    }

    if(!seader_temp_strings_ensure(seader, 2U)) {
        FURI_LOG_E(TAG, "Temp string allocation failed");
        return;
    }

    FuriString* title = seader->temp_string1;
    FuriString* body = seader->temp_string2;
    furi_string_reset(title);
    furi_string_set_str(
        title, seader->uhf_module_label[0] != '\0' ? seader->uhf_module_label : "UHF");
    seader_scene_uhf_info_format_body(seader, body);

    widget_add_button_element(
        widget, GuiButtonTypeLeft, "Back", seader_scene_uhf_info_widget_callback, seader);
    widget_add_string_element(
        widget, 64, 13, AlignCenter, AlignCenter, FontPrimary, furi_string_get_cstr(title));
    widget_add_text_box_element(
        widget, 5, 22, 118, 26, AlignCenter, AlignTop, furi_string_get_cstr(body), false);
    view_dispatcher_switch_to_view(seader->view_dispatcher, SeaderViewWidget);
}

bool seader_scene_uhf_info_on_event(void* context, SceneManagerEvent event) {
    Seader* seader = context;
    furi_check(seader);

    bool consumed = false;
    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == GuiButtonTypeLeft) {
            consumed = scene_manager_previous_scene(seader->scene_manager);
        } else if(event.event == SeaderCustomEventSamStatusUpdated) {
            seader_scene_uhf_info_on_exit(context);
            seader_scene_uhf_info_on_enter(context);
            consumed = true;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        consumed = scene_manager_search_and_switch_to_previous_scene(
            seader->scene_manager, SeaderSceneSamPresent);
    }

    return consumed;
}

void seader_scene_uhf_info_on_exit(void* context) {
    Seader* seader = context;
    if(seader) {
        if(seader->widget) {
            widget_reset(seader->widget);
        }
        seader_temp_strings_release(seader, 2U);
    }
}
