/*
 * SPDX-License-Identifier: MIT
 */

#include "add_device_screen.h"

#include "input_keys.h"

#include <string>

namespace screen {

AddDeviceScreen::AddDeviceScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

void AddDeviceScreen::build_content(lv_obj_t* content) {
    prompt_ = lv_label_create(content);
    lv_label_set_text(prompt_, "WLED IP address");
    lv_obj_set_style_text_font(prompt_, font(14, true), 0);
    lv_obj_align(prompt_, LV_ALIGN_TOP_LEFT, 16, 10);

    field_ = lv_label_create(content);
    lv_obj_set_size(field_, 288, 30);
    lv_obj_set_style_text_font(field_, font(20), 0);
    lv_obj_set_style_pad_left(field_, 10, 0);
    lv_obj_set_style_pad_top(field_, 4, 0);
    lv_obj_set_style_radius(field_, 4, 0);
    lv_obj_set_style_border_width(field_, 2, 0);
    lv_obj_set_style_bg_opa(field_, LV_OPA_COVER, 0);
    lv_obj_align(field_, LV_ALIGN_TOP_MID, 0, 32);

    hint_ = lv_label_create(content);
    lv_label_set_text(hint_, "Digits and .   ENTER add   ESC cancel");
    lv_obj_set_style_text_font(hint_, font(12), 0);
    lv_obj_align(hint_, LV_ALIGN_TOP_LEFT, 16, 72);
}

void AddDeviceScreen::refresh() {
    const auto palette = colors();
    const std::string text = view_model().ip_draft() + "_";
    lv_label_set_text(field_, text.c_str());
    lv_obj_set_style_text_color(field_, palette.text, 0);
    lv_obj_set_style_bg_color(field_, palette.background, 0);
    lv_obj_set_style_border_color(field_, palette.primary, 0);
    lv_obj_set_style_text_color(prompt_, palette.text, 0);
    lv_obj_set_style_text_color(hint_, palette.text_disabled, 0);
}

bool AddDeviceScreen::handle_key(uint32_t key) {
    if ((key >= '0' && key <= '9') || key == '.') {
        view_model().ip_draft_append(static_cast<char>(key));
        return true;
    }
    if (key == ',') { // easier to reach than '.' on some layouts
        view_model().ip_draft_append('.');
        return true;
    }
    switch (view::classify_key(key)) {
        case view::NavKey::Backspace:
            view_model().ip_draft_backspace();
            return true;
        case view::NavKey::Enter:
            view_model().ip_draft_submit();
            return true;
        default:
            return true; // text entry: swallow everything else so nav keys don't fire
    }
}

} // namespace screen
