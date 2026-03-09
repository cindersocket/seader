#include "../seader_i.h"

static void seader_scene_read_uhf_result_widget_callback(
    GuiButtonType result,
    InputType type,
    void* context) {
    Seader* seader = context;
    if(type == InputTypeShort) {
        view_dispatcher_send_custom_event(seader->view_dispatcher, result);
    }
}

void seader_scene_read_uhf_result_on_enter(void* context) {
    Seader* seader = context;
    Widget* widget = seader->widget;
    FuriString* line1 = seader->temp_string1;
    FuriString* line2 = seader->temp_string2;
    FuriString* line3 = seader->temp_string3;

    widget_reset(widget);
    furi_string_printf(line1, "UHF %s", seader->uhf_result_label[0] ? seader->uhf_result_label : "Data");
    furi_string_reset(line2);
    furi_string_reset(line3);

    for(size_t i = 0; i < seader->uhf_result_len; i++) {
        FuriString* target = i < 8 ? line2 : line3;
        furi_string_cat_printf(target, "%02X", seader->uhf_result_data[i]);
    }

    widget_add_button_element(
        widget, GuiButtonTypeLeft, "Retry", seader_scene_read_uhf_result_widget_callback, seader);
    widget_add_button_element(
        widget, GuiButtonTypeRight, "Back", seader_scene_read_uhf_result_widget_callback, seader);
    widget_add_string_element(
        widget, 64, 10, AlignCenter, AlignCenter, FontPrimary, furi_string_get_cstr(line1));
    widget_add_string_element(
        widget, 64, 26, AlignCenter, AlignCenter, FontSecondary, furi_string_get_cstr(line2));
    if(!furi_string_empty(line3)) {
        widget_add_string_element(
            widget, 64, 40, AlignCenter, AlignCenter, FontSecondary, furi_string_get_cstr(line3));
    }

    view_dispatcher_switch_to_view(seader->view_dispatcher, SeaderViewWidget);
}

bool seader_scene_read_uhf_result_on_event(void* context, SceneManagerEvent event) {
    Seader* seader = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == GuiButtonTypeLeft) {
            consumed = scene_manager_previous_scene(seader->scene_manager);
        } else if(event.event == GuiButtonTypeRight) {
            seader->selected_read_type = SeaderCredentialTypeNone;
            seader->read_scope = SeaderReadScopeAll;
            seader->uhf_read_mode = SeaderUhfReadModeNone;
            consumed = scene_manager_search_and_switch_to_previous_scene(
                seader->scene_manager, SeaderSceneReadUhfMenu);
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        seader->selected_read_type = SeaderCredentialTypeNone;
        seader->read_scope = SeaderReadScopeAll;
        seader->uhf_read_mode = SeaderUhfReadModeNone;
        consumed = scene_manager_search_and_switch_to_previous_scene(
            seader->scene_manager, SeaderSceneReadUhfMenu);
    }

    return consumed;
}

void seader_scene_read_uhf_result_on_exit(void* context) {
    Seader* seader = context;
    widget_reset(seader->widget);
}
