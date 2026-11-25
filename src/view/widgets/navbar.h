/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_widget.h"
#include "base_viewmodel.h"
#include "icon_button.h"

#include <array>
#include <memory>

namespace app {
class AssetManager;
}

namespace view::widgets {

// Bottom bar: five icons aligned with number keys 4..8 (CardputerZero key guideline), or a
// text hint row on text-entry pages.
class NavBar : public BaseWidgets {
public:
    NavBar(lv_obj_t* parent, viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~NavBar() override;

    void build() override;

private:
    void create_icon_buttons();
    void create_shortcut_hints();
    void update_icon_buttons();

    static void nav_clicked_cb(lv_event_t* event);
    static void update_icons_cb(lv_observer_t* observer, lv_subject_t* subject);

    viewmodel::BaseViewModel& view_model_;
    app::AssetManager& assets_;
    std::array<std::unique_ptr<IconButton>, 5> icon_buttons_;
    std::array<lv_obj_t*, 3> shortcut_key_labels_{};
    std::array<lv_obj_t*, 3> shortcut_action_labels_{};
    lv_font_t* icon_font_{nullptr};
    lv_obj_t* shortcut_bar_{nullptr};
    lv_observer_t* page_observer_{nullptr};
    lv_observer_t* theme_observer_{nullptr};
    int shown_page_{-1};
    bool shown_dark_{false};
};

} // namespace view::widgets
