/*
 * SPDX-License-Identifier: MIT
 */

#include "devices_screen.h"

#include "input_keys.h"

#include <cmath>
#include <cstdio>

namespace screen {
namespace {

constexpr int32_t kRowHeight = 22;

int percent(int bri) {
    return static_cast<int>(std::lround(bri * 100.0 / 255.0));
}

} // namespace

DevicesScreen::DevicesScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

void DevicesScreen::build_content(lv_obj_t* content) {
    list_ = lv_obj_create(content);
    lv_obj_remove_style_all(list_);
    lv_obj_set_size(list_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(list_, 2, 0);
    lv_obj_set_style_pad_row(list_, 0, 0);
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_OFF);

    empty_label_ = lv_label_create(content);
    lv_obj_set_width(empty_label_, 290);
    lv_label_set_long_mode(empty_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(empty_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(empty_label_, font(14), 0);
    lv_obj_center(empty_label_);
}

DevicesScreen::Row DevicesScreen::create_row() {
    Row row;
    row.row = lv_obj_create(list_);
    lv_obj_remove_style_all(row.row);
    lv_obj_set_size(row.row, LV_PCT(100), kRowHeight);
    lv_obj_set_style_radius(row.row, 4, 0);
    lv_obj_clear_flag(row.row, LV_OBJ_FLAG_SCROLLABLE);

    row.swatch = lv_obj_create(row.row);
    lv_obj_remove_style_all(row.swatch);
    lv_obj_set_size(row.swatch, 10, 10);
    lv_obj_set_style_radius(row.swatch, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(row.swatch, LV_OPA_COVER, 0);
    lv_obj_align(row.swatch, LV_ALIGN_LEFT_MID, 6, 0);

    row.name = lv_label_create(row.row);
    lv_label_set_long_mode(row.name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(row.name, 140);
    lv_obj_set_style_text_font(row.name, font(14, true), 0);
    lv_obj_align(row.name, LV_ALIGN_LEFT_MID, 22, 0);

    row.ip = lv_label_create(row.row);
    lv_label_set_long_mode(row.ip, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(row.ip, 96);
    lv_obj_set_style_text_font(row.ip, font(12), 0);
    lv_obj_align(row.ip, LV_ALIGN_LEFT_MID, 166, 0);

    row.status = lv_label_create(row.row);
    lv_obj_set_width(row.status, 54);
    lv_obj_set_style_text_align(row.status, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(row.status, font(12, true), 0);
    lv_obj_align(row.status, LV_ALIGN_RIGHT_MID, -4, 0);
    return row;
}

void DevicesScreen::refresh() {
    const auto palette = colors();
    const auto& devices = view_model().devices();

    if (devices.empty()) {
        lv_obj_add_flag(list_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(empty_label_, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(empty_label_,
                          view_model().scanning()
                              ? "Looking for WLED devices..."
                              : "No WLED devices found.\n6  Rescan     7  Add by IP");
        lv_obj_set_style_text_color(empty_label_, palette.text, 0);
        return;
    }
    lv_obj_add_flag(empty_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(list_, LV_OBJ_FLAG_HIDDEN);

    while (rows_.size() < devices.size()) {
        rows_.push_back(create_row());
    }
    while (rows_.size() > devices.size()) {
        lv_obj_delete(rows_.back().row);
        rows_.pop_back();
    }

    const int cursor = view_model().list_cursor();
    for (std::size_t i = 0; i < devices.size(); ++i) {
        const auto& device = devices[i];
        auto& row = rows_[i];
        const bool selected = static_cast<int>(i) == cursor;

        lv_obj_set_style_bg_color(row.row, palette.primary, 0);
        lv_obj_set_style_bg_opa(row.row, selected ? LV_OPA_20 : LV_OPA_TRANSP, 0);

        lv_label_set_text(row.name, device.display_name().c_str());
        lv_obj_set_style_text_color(row.name, selected ? palette.primary : palette.text, 0);
        lv_label_set_text(row.ip, device.ip.c_str());
        lv_obj_set_style_text_color(row.ip, palette.text_disabled, 0);

        char status[24];
        lv_color_t status_color = palette.text;
        lv_color_t swatch = palette.text_disabled;
        if (!device.verified) {
            std::snprintf(status, sizeof(status), "%s", device.consecutive_failures ? "no reply" : "...");
            status_color = device.consecutive_failures ? palette.error : palette.text_disabled;
        }
        else if (!device.online) {
            std::snprintf(status, sizeof(status), "offline");
            status_color = palette.error;
        }
        else if (device.state.on) {
            std::snprintf(status, sizeof(status), "%d%%", percent(device.state.bri));
            status_color = palette.success;
            swatch = lv_color_make(device.state.color.r, device.state.color.g, device.state.color.b);
        }
        else {
            std::snprintf(status, sizeof(status), "OFF");
            status_color = palette.text_disabled;
        }
        lv_label_set_text(row.status, status);
        lv_obj_set_style_text_color(row.status, status_color, 0);
        lv_obj_set_style_bg_color(row.swatch, swatch, 0);
        lv_obj_set_style_border_width(row.swatch, 1, 0);
        lv_obj_set_style_border_color(row.swatch, palette.border, 0);
    }

    if (cursor >= 0 && cursor < static_cast<int>(rows_.size())) {
        lv_obj_scroll_to_view(rows_[static_cast<std::size_t>(cursor)].row, LV_ANIM_OFF);
    }
}

bool DevicesScreen::handle_key(uint32_t key) {
    switch (view::classify_key(key)) {
        case view::NavKey::Up:
            view_model().move_list_cursor(-1);
            return true;
        case view::NavKey::Down:
            view_model().move_list_cursor(1);
            return true;
        case view::NavKey::Right:
        case view::NavKey::Enter:
            view_model().open_cursor_device();
            return true;
        case view::NavKey::Space:
            view_model().toggle_power_at_cursor();
            return true;
        default:
            return false;
    }
}

} // namespace screen
