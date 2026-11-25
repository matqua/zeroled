/*
 * SPDX-License-Identifier: MIT
 */

#include "effects_screen.h"

#include "input_keys.h"

#include <cstdio>

namespace screen {
namespace {

constexpr int32_t kRowHeight = 18;

} // namespace

EffectsScreen::EffectsScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

void EffectsScreen::build_content(lv_obj_t* content) {
    list_ = lv_obj_create(content);
    lv_obj_remove_style_all(list_);
    lv_obj_set_size(list_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(list_, 2, 0);
    lv_obj_set_style_pad_row(list_, 0, 0);
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_ACTIVE);

    empty_label_ = lv_label_create(content);
    lv_obj_set_style_text_font(empty_label_, font(14), 0);
    lv_obj_center(empty_label_);
}

void EffectsScreen::rebuild_rows() {
    const auto* device = view_model().selected_device();
    const auto& order = view_model().effect_order();
    if (device && built_for_ip_ == device->ip && row_ids_ == order) {
        return;
    }
    for (auto* row : rows_) {
        lv_obj_delete(row);
    }
    rows_.clear();
    row_ids_ = order;
    built_for_ip_ = device ? device->ip : std::string();
    if (!device) {
        return;
    }

    const auto* row_font = font(12);
    for (const int id : order) {
        auto* row = lv_label_create(list_);
        lv_obj_set_size(row, LV_PCT(100), kRowHeight);
        lv_label_set_long_mode(row, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(row, row_font, 0);
        lv_obj_set_style_pad_left(row, 8, 0);
        lv_obj_set_style_pad_top(row, 2, 0);
        lv_obj_set_style_radius(row, 3, 0);
        lv_label_set_text(row, device->effects[static_cast<std::size_t>(id)].c_str());
        rows_.push_back(row);
    }
}

void EffectsScreen::refresh() {
    const auto palette = colors();
    const auto* device = view_model().selected_device();
    rebuild_rows();

    if (rows_.empty()) {
        lv_obj_add_flag(list_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(empty_label_, LV_OBJ_FLAG_HIDDEN);
        const bool loading = device && !device->effects_loaded;
        lv_label_set_text(empty_label_, loading ? "Loading effects..." : "No effects reported");
        lv_obj_set_style_text_color(empty_label_, palette.text, 0);
        return;
    }
    lv_obj_add_flag(empty_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(list_, LV_OBJ_FLAG_HIDDEN);

    const int cursor = view_model().effect_cursor();
    const int active = device && device->has_state ? device->state.fx : -1;
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const bool selected = static_cast<int>(i) == cursor;
        const bool is_active = row_ids_[i] == active;
        lv_obj_set_style_bg_color(rows_[i], palette.primary, 0);
        lv_obj_set_style_bg_opa(rows_[i], selected ? LV_OPA_20 : LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(rows_[i],
                                    is_active ? palette.success : (selected ? palette.primary : palette.text),
                                    0);
        lv_obj_set_style_text_decor(rows_[i], is_active ? LV_TEXT_DECOR_UNDERLINE : LV_TEXT_DECOR_NONE, 0);
    }
    if (cursor >= 0 && cursor < static_cast<int>(rows_.size())) {
        lv_obj_scroll_to_view(rows_[static_cast<std::size_t>(cursor)], LV_ANIM_OFF);
    }
}

bool EffectsScreen::handle_key(uint32_t key) {
    switch (view::classify_key(key)) {
        case view::NavKey::Up:
            view_model().move_effect_cursor(-1);
            return true;
        case view::NavKey::Down:
            view_model().move_effect_cursor(1);
            return true;
        case view::NavKey::Left:
            view_model().move_effect_cursor(-5);
            return true;
        case view::NavKey::Right:
            view_model().move_effect_cursor(5);
            return true;
        case view::NavKey::Enter:
        case view::NavKey::Space:
            view_model().apply_effect_at_cursor();
            return true;
        default:
            return false;
    }
}

} // namespace screen
