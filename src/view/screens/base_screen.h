/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "navbar.h"
#include "base_viewmodel.h"
#include "titlebar.h"
#include "theme.h"
#include "lvgl.h"

#include <memory>

namespace app {
class AssetManager;
enum class StandardFontWeight;
}

namespace screen {

class BaseScreen {
public:
    BaseScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    virtual ~BaseScreen();

    BaseScreen(const BaseScreen&) = delete;
    BaseScreen& operator=(const BaseScreen&) = delete;

    void init();
    lv_obj_t* root() const;

    // Page-specific keys. Return true when consumed; unconsumed keys 4..8 reach the nav bar.
    virtual bool handle_key(uint32_t key) {
        LV_UNUSED(key);
        return false;
    }

protected:
    virtual void build_content(lv_obj_t* content) = 0;
    // Called after build and whenever the view model revision or theme changes.
    virtual void refresh() {}

    viewmodel::BaseViewModel& view_model();
    app::AssetManager& assets();
    view::ThemePalette colors() const;
    // Never null: falls back to the built-in Montserrat font of the nearest size.
    const lv_font_t* font(uint32_t size, bool bold = false);

private:
    static void refresh_observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    viewmodel::BaseViewModel& view_model_;
    app::AssetManager& assets_;
    lv_obj_t* root_{nullptr};
    lv_obj_t* content_{nullptr};
    std::unique_ptr<view::widgets::TitleBar> title_bar_;
    std::unique_ptr<view::widgets::NavBar> nav_bar_;
};

} // namespace screen
