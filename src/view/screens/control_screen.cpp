/*
 * SPDX-License-Identifier: MIT
 */

#include "control_screen.h"

#include "color.h"
#include "input_keys.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace screen {
namespace {

using viewmodel::ControlRow;

constexpr int32_t kRowHeight = 18;
constexpr int32_t kBarWidth = 120;
constexpr int32_t kBarX = 96;

int percent(int value, int max) {
    return max <= 0 ? 0 : static_cast<int>(std::lround(value * 100.0 / max));
}

std::size_t index(ControlRow row) {
    return static_cast<std::size_t>(row);
}

} // namespace

ControlScreen::ControlScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

ControlScreen::Row ControlScreen::create_row(const char* name, bool with_bar, bool with_swatch) {
    Row row;
    row.row = lv_obj_create(list_);
    lv_obj_remove_style_all(row.row);
    lv_obj_set_size(row.row, LV_PCT(100), kRowHeight);
    lv_obj_set_style_radius(row.row, 3, 0);
    lv_obj_clear_flag(row.row, LV_OBJ_FLAG_SCROLLABLE);

    row.name = lv_label_create(row.row);
    lv_label_set_text(row.name, name);
    lv_obj_set_style_text_font(row.name, font(12, true), 0);
    lv_obj_align(row.name, LV_ALIGN_LEFT_MID, 8, 0);

    if (with_bar) {
        row.track = lv_obj_create(row.row);
        lv_obj_remove_style_all(row.track);
        lv_obj_set_size(row.track, kBarWidth, 6);
        lv_obj_set_style_radius(row.track, 3, 0);
        lv_obj_set_style_bg_opa(row.track, LV_OPA_COVER, 0);
        lv_obj_align(row.track, LV_ALIGN_LEFT_MID, kBarX, 0);

        row.fill = lv_obj_create(row.track);
        lv_obj_remove_style_all(row.fill);
        lv_obj_set_height(row.fill, 6);
        lv_obj_set_style_radius(row.fill, 3, 0);
        lv_obj_set_style_bg_opa(row.fill, LV_OPA_COVER, 0);
        lv_obj_align(row.fill, LV_ALIGN_LEFT_MID, 0, 0);
    }
    if (with_swatch) {
        row.swatch = lv_obj_create(row.row);
        lv_obj_remove_style_all(row.swatch);
        lv_obj_set_size(row.swatch, kBarWidth, 10);
        lv_obj_set_style_radius(row.swatch, 3, 0);
        lv_obj_set_style_bg_opa(row.swatch, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row.swatch, 1, 0);
        lv_obj_align(row.swatch, LV_ALIGN_LEFT_MID, kBarX, 0);
    }

    row.value = lv_label_create(row.row);
    lv_label_set_long_mode(row.value, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(row.value, font(12), 0);
    if (with_bar || with_swatch) {
        lv_obj_set_width(row.value, 76);
        lv_obj_set_style_text_align(row.value, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(row.value, LV_ALIGN_RIGHT_MID, -8, 0);
    }
    else {
        lv_obj_set_width(row.value, 206);
        lv_obj_set_style_text_align(row.value, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_align(row.value, LV_ALIGN_LEFT_MID, kBarX, 0);
    }
    return row;
}

void ControlScreen::build_content(lv_obj_t* content) {
    list_ = lv_obj_create(content);
    lv_obj_remove_style_all(list_);
    lv_obj_set_size(list_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(list_, 2, 0);
    lv_obj_set_style_pad_row(list_, 0, 0);
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_OFF);

    rows_[index(ControlRow::Power)] = create_row("Power", false, false);
    rows_[index(ControlRow::Brightness)] = create_row("Brightness", true, false);
    rows_[index(ControlRow::Hue)] = create_row("Colour", false, true);
    rows_[index(ControlRow::Saturation)] = create_row("Saturation", true, false);
    rows_[index(ControlRow::Effect)] = create_row("Effect", false, false);
    rows_[index(ControlRow::Speed)] = create_row("Speed", true, false);
    rows_[index(ControlRow::Intensity)] = create_row("Intensity", true, false);
    rows_[index(ControlRow::SyncSend)] = create_row("Sync send", false, false);
    rows_[index(ControlRow::SyncReceive)] = create_row("Sync receive", false, false);

    overlay_ = lv_label_create(content);
    lv_obj_set_style_text_font(overlay_, font(12, true), 0);
    lv_obj_set_style_radius(overlay_, 3, 0);
    lv_obj_set_style_pad_hor(overlay_, 6, 0);
    lv_obj_set_style_pad_ver(overlay_, 2, 0);
    lv_obj_set_style_bg_opa(overlay_, LV_OPA_COVER, 0);
    lv_obj_align(overlay_, LV_ALIGN_BOTTOM_RIGHT, -4, -2);
}

void ControlScreen::set_bar(Row& row, int value, int max, lv_color_t color) {
    const auto palette = colors();
    lv_obj_set_style_bg_color(row.track, palette.border, 0);
    lv_obj_set_style_bg_color(row.fill, color, 0);
    const int32_t width = max <= 0 ? 0 : static_cast<int32_t>(kBarWidth * value / max);
    lv_obj_set_width(row.fill, std::max<int32_t>(width, value > 0 ? 3 : 0));
}

void ControlScreen::refresh() {
    const auto palette = colors();
    const auto* device = view_model().selected_device();
    const int cursor = view_model().control_cursor();

    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const bool selected = static_cast<int>(i) == cursor;
        lv_obj_set_style_bg_color(rows_[i].row, palette.primary, 0);
        lv_obj_set_style_bg_opa(rows_[i].row, selected ? LV_OPA_20 : LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(rows_[i].name, selected ? palette.primary : palette.text, 0);
        lv_obj_set_style_text_color(rows_[i].value, palette.text, 0);
    }
    lv_obj_scroll_to_view(rows_[static_cast<std::size_t>(cursor)].row, LV_ANIM_OFF);

    if (!device || !device->has_state) {
        for (auto& row : rows_) {
            lv_label_set_text(row.value, "-");
            if (row.fill) {
                set_bar(row, 0, 1, palette.primary);
            }
        }
        lv_label_set_text(overlay_, device && device->consecutive_failures ? "NOT RESPONDING" : "CONNECTING");
        lv_obj_set_style_bg_color(overlay_, palette.warning, 0);
        lv_obj_set_style_text_color(overlay_, palette.background, 0);
        lv_obj_remove_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    const auto& state = device->state;
    char text[64];

    auto& power = rows_[index(ControlRow::Power)];
    lv_label_set_text(power.value, state.on ? "ON" : "OFF");
    lv_obj_set_style_text_color(power.value, state.on ? palette.success : palette.text_disabled, 0);

    auto& brightness = rows_[index(ControlRow::Brightness)];
    std::snprintf(text, sizeof(text), "%d%%", percent(state.bri, 255));
    lv_label_set_text(brightness.value, text);
    set_bar(brightness, state.bri, 255, palette.primary);

    const float hue = view_model().selected_hue();
    const float saturation = view_model().selected_saturation();
    auto& colour = rows_[index(ControlRow::Hue)];
    lv_obj_set_style_bg_color(colour.swatch, lv_color_make(state.color.r, state.color.g, state.color.b), 0);
    lv_obj_set_style_border_color(colour.swatch, palette.border, 0);
    if (saturation < 0.05F) {
        std::snprintf(text, sizeof(text), "white");
    }
    else {
        std::snprintf(text, sizeof(text), "%d\xC2\xB0", static_cast<int>(std::lround(hue)) % 360);
    }
    lv_label_set_text(colour.value, text);

    auto& sat = rows_[index(ControlRow::Saturation)];
    std::snprintf(text, sizeof(text), "%d%%", static_cast<int>(std::lround(saturation * 100.0F)));
    lv_label_set_text(sat.value, text);
    const auto sat_color = model::hsv_to_rgb({hue, 1.0F, 1.0F});
    set_bar(sat, static_cast<int>(std::lround(saturation * 100.0F)), 100,
            lv_color_make(sat_color.r, sat_color.g, sat_color.b));

    auto& effect = rows_[index(ControlRow::Effect)];
    std::string effect_name;
    if (state.fx >= 0 && static_cast<std::size_t>(state.fx) < device->effects.size()) {
        effect_name = device->effects[static_cast<std::size_t>(state.fx)];
    }
    else {
        effect_name = "#" + std::to_string(state.fx);
    }
    const std::string effect_text = "< " + effect_name + " >";
    lv_label_set_text(effect.value, effect_text.c_str());

    auto& speed = rows_[index(ControlRow::Speed)];
    std::snprintf(text, sizeof(text), "%d", state.sx);
    lv_label_set_text(speed.value, text);
    set_bar(speed, state.sx, 255, palette.info);

    auto& intensity = rows_[index(ControlRow::Intensity)];
    std::snprintf(text, sizeof(text), "%d", state.ix);
    lv_label_set_text(intensity.value, text);
    set_bar(intensity, state.ix, 255, palette.info);

    auto& sync_send = rows_[index(ControlRow::SyncSend)];
    lv_label_set_text(sync_send.value, state.sync_send ? "ON" : "OFF");
    lv_obj_set_style_text_color(sync_send.value, state.sync_send ? palette.success : palette.text_disabled, 0);

    auto& sync_recv = rows_[index(ControlRow::SyncReceive)];
    lv_label_set_text(sync_recv.value, state.sync_recv ? "ON" : "OFF");
    lv_obj_set_style_text_color(sync_recv.value, state.sync_recv ? palette.success : palette.text_disabled, 0);

    if (!device->online) {
        lv_label_set_text(overlay_, "OFFLINE");
        lv_obj_set_style_bg_color(overlay_, palette.error, 0);
        lv_obj_set_style_text_color(overlay_, palette.background, 0);
        lv_obj_remove_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
    }
    else if (device->info.live) {
        const std::string live = "LIVE " + (device->info.live_mode.empty() ? std::string("stream") : device->info.live_mode);
        lv_label_set_text(overlay_, live.c_str());
        lv_obj_set_style_bg_color(overlay_, palette.warning, 0);
        lv_obj_set_style_text_color(overlay_, palette.background, 0);
        lv_obj_remove_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
    }
}

bool ControlScreen::handle_key(uint32_t key) {
    switch (view::classify_key(key)) {
        case view::NavKey::Up:
            view_model().move_control_cursor(-1);
            return true;
        case view::NavKey::Down:
            view_model().move_control_cursor(1);
            return true;
        case view::NavKey::Left:
            view_model().adjust_control(-1);
            return true;
        case view::NavKey::Right:
            view_model().adjust_control(1);
            return true;
        case view::NavKey::Enter:
            view_model().activate_control();
            return true;
        case view::NavKey::Space:
            view_model().toggle_power();
            return true;
        default:
            return false;
    }
}

} // namespace screen
