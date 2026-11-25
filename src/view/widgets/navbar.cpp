/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "navbar.h"

#include "asset_manager.h"
#include "bindings.h"
#include "linux_input.h"
#include "theme.h"
#include "ui_const.h"

namespace view::widgets {
namespace {

// Puts icon centres at x = 49, 105, 161, 217, 273 (the recommended positions above keys 4..8).
constexpr std::array<int32_t, 5> kNavButtonXOffsets = {30, 17, 2, -15, -30};

struct ShortcutSpec {
    const char* key;
    const char* action;
};

constexpr std::array<ShortcutSpec, 3> kAddDeviceShortcuts = {{
    {"ESC", "Cancel"},
    {"DEL", "Erase"},
    {"ENTER", "Add"},
}};

using Icons = std::array<const char*, 5>;

Icons icons_for(model::AppPage page) {
    switch (page) {
        case model::AppPage::Devices:
            return {ICON_SIGN_OUT, ICON_POWER, ICON_ARROWS_CLOCKWISE, ICON_PLUS, ICON_SLIDERS};
        case model::AppPage::Control:
            return {ICON_ARROW_LEFT, ICON_POWER, ICON_BROADCAST, ICON_INFO, ICON_MAGIC_WAND};
        case model::AppPage::Effects:
            return {ICON_ARROW_LEFT, ICON_CARET_DOUBLE_UP, nullptr, ICON_CARET_DOUBLE_DOWN, ICON_CHECK};
        case model::AppPage::Info:
            return {ICON_ARROW_LEFT, ICON_POWER, nullptr, nullptr, ICON_SLIDERS};
        case model::AppPage::AddDevice:
            return {nullptr, nullptr, nullptr, nullptr, nullptr};
    }
    return {};
}

} // namespace

NavBar::NavBar(lv_obj_t* parent, viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseWidgets(parent), view_model_(view_model), assets_(assets) {}

NavBar::~NavBar() {
    for (size_t i = 0; i < icon_buttons_.size(); ++i) {
        if (icon_buttons_[i]) {
            platform::unregister_nav_button(i, icon_buttons_[i]->root());
        }
    }

    if (page_observer_) {
        lv_observer_remove(page_observer_);
        page_observer_ = nullptr;
    }
    if (theme_observer_) {
        lv_observer_remove(theme_observer_);
        theme_observer_ = nullptr;
    }
}

void NavBar::build() {
    if (core_obj_) {
        return;
    }

    core_obj_ = lv_obj_create(parent_);
    lv_obj_remove_style_all(core_obj_);
    lv_obj_set_size(core_obj_, LV_PCT(100), kNavBarHeight);
    lv_obj_align(core_obj_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(core_obj_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(core_obj_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(core_obj_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(core_obj_, 8, 0);
    lv_obj_set_style_pad_right(core_obj_, 8, 0);
    reactive::bind_theme(core_obj_, view_model_.dark_mode_subject(), reactive::ThemeRole::Bar);

    create_icon_buttons();
    create_shortcut_hints();
    page_observer_ = reactive::observe_obj(core_obj_, view_model_.current_page_subject(), update_icons_cb, this);
    theme_observer_ = reactive::observe_obj(core_obj_, view_model_.dark_mode_subject(), update_icons_cb, this);
    update_icon_buttons();
}

void NavBar::create_shortcut_hints() {
    shortcut_bar_ = lv_obj_create(core_obj_);
    lv_obj_remove_style_all(shortcut_bar_);
    lv_obj_set_size(shortcut_bar_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(shortcut_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(shortcut_bar_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(shortcut_bar_, 10, 0);
    lv_obj_clear_flag(shortcut_bar_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(shortcut_bar_, LV_OBJ_FLAG_IGNORE_LAYOUT);

    const lv_font_t* key_font = assets_.load_standard_font(14);
    if (!key_font) {
        key_font = &lv_font_montserrat_14;
    }
    const lv_font_t* text_font = assets_.load_standard_font(14, app::StandardFontWeight::Regular);
    if (!text_font) {
        text_font = &lv_font_montserrat_14;
    }

    for (size_t index = 0; index < kAddDeviceShortcuts.size(); ++index) {
        auto* item = lv_obj_create(shortcut_bar_);
        lv_obj_remove_style_all(item);
        lv_obj_set_size(item, LV_SIZE_CONTENT, LV_PCT(100));
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(item, 4, 0);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

        auto* key = lv_label_create(item);
        lv_label_set_text(key, kAddDeviceShortcuts[index].key);
        lv_obj_set_style_text_font(key, key_font, 0);
        shortcut_key_labels_[index] = key;

        auto* label = lv_label_create(item);
        lv_label_set_text(label, kAddDeviceShortcuts[index].action);
        lv_obj_set_style_text_font(label, text_font, 0);
        shortcut_action_labels_[index] = label;
    }
}

void NavBar::create_icon_buttons() {
    const auto light_color = view::palette(false).text;
    const auto dark_color = view::palette(true).text;
    icon_font_ = assets_.load_font("Phosphor-Fill.ttf", 22);

    for (size_t i = 0; i < icon_buttons_.size(); ++i) {
        icon_buttons_[i] = std::make_unique<IconButton>(core_obj_,
                                                        view_model_,
                                                        32,
                                                        24,
                                                        "",
                                                        icon_font_ ? icon_font_ : &lv_font_montserrat_14,
                                                        light_color,
                                                        dark_color,
                                                        nullptr,
                                                        &view_model_,
                                                        true);
        icon_buttons_[i]->build();
        lv_obj_set_style_translate_x(icon_buttons_[i]->root(), kNavButtonXOffsets[i], 0);
        lv_obj_add_event_cb(icon_buttons_[i]->root(), nav_clicked_cb, LV_EVENT_CLICKED, this);
        platform::register_nav_button(i, icon_buttons_[i]->root());
    }
}

void NavBar::update_icon_buttons() {
    const auto page = view_model_.current_page();
    const bool dark = view_model_.is_dark_mode();
    if (shown_page_ == static_cast<int>(page) && shown_dark_ == dark) {
        return;
    }
    shown_page_ = static_cast<int>(page);
    shown_dark_ = dark;

    const auto colors = view::palette(dark);
    const bool text_entry = page == model::AppPage::AddDevice;
    platform::set_nav_shortcut_mode(false);

    for (size_t i = 0; i < shortcut_key_labels_.size(); ++i) {
        if (shortcut_key_labels_[i]) {
            lv_obj_set_style_text_color(shortcut_key_labels_[i], colors.primary, 0);
        }
        if (shortcut_action_labels_[i]) {
            lv_obj_set_style_text_color(shortcut_action_labels_[i], colors.text, 0);
        }
    }
    if (text_entry) {
        lv_obj_remove_flag(shortcut_bar_, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_flag(shortcut_bar_, LV_OBJ_FLAG_HIDDEN);
    }

    const auto icons = icons_for(page);
    for (size_t i = 0; i < icon_buttons_.size(); ++i) {
        auto& button = icon_buttons_[i];
        if (!button || !button->root()) {
            continue;
        }
        const bool has_action = icons[i] != nullptr;
        button->set_text(has_action ? icons[i] : "");
        button->set_enabled(has_action);
        if (text_entry) {
            lv_obj_add_flag(button->root(), LV_OBJ_FLAG_HIDDEN);
        }
        else {
            lv_obj_remove_flag(button->root(), LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void NavBar::nav_clicked_cb(lv_event_t* event) {
    auto* nav_bar = static_cast<NavBar*>(lv_event_get_user_data(event));
    if (!nav_bar) {
        return;
    }
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
    for (size_t i = 0; i < nav_bar->icon_buttons_.size(); ++i) {
        if (nav_bar->icon_buttons_[i] && nav_bar->icon_buttons_[i]->root() == target) {
            nav_bar->view_model_.nav_action(static_cast<int>(i));
            return;
        }
    }
}

void NavBar::update_icons_cb(lv_observer_t* observer, lv_subject_t* subject) {
    LV_UNUSED(subject);

    auto* nav_bar = static_cast<NavBar*>(lv_observer_get_user_data(observer));
    if (nav_bar) {
        nav_bar->update_icon_buttons();
    }
}

} // namespace view::widgets
