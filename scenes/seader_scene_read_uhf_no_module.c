#include "../seader_i.h"

static void seader_scene_read_uhf_no_module_widget_callback(
    GuiButtonType result,
    InputType type,
    void* context) {
    Seader* seader = context;
    if(type == InputTypeShort) {
        view_dispatcher_send_custom_event(seader->view_dispatcher, result);
    }
}

void seader_scene_read_uhf_no_module_on_enter(void* context) {
    Seader* seader = context;
    Widget* widget = seader->widget;

    widget_reset(widget);
    widget_add_button_element(
        widget,
        GuiButtonTypeLeft,
        "Back",
        seader_scene_read_uhf_no_module_widget_callback,
        seader);
    widget_add_string_element(
        widget, 64, 18, AlignCenter, AlignCenter, FontPrimary, "No UHF module");
    widget_add_string_element(
        widget, 64, 34, AlignCenter, AlignCenter, FontSecondary, "found");
    view_dispatcher_switch_to_view(seader->view_dispatcher, SeaderViewWidget);
}

bool seader_scene_read_uhf_no_module_on_event(void* context, SceneManagerEvent event) {
    Seader* seader = context;

    if(event.type == SceneManagerEventTypeCustom && event.event == GuiButtonTypeLeft) {
        return scene_manager_previous_scene(seader->scene_manager);
    }

    if(event.type == SceneManagerEventTypeBack) {
        return scene_manager_previous_scene(seader->scene_manager);
    }

    return false;
}

void seader_scene_read_uhf_no_module_on_exit(void* context) {
    Seader* seader = context;
    widget_reset(seader->widget);
}
