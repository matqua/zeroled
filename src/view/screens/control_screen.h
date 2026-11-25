/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"

#include <array>

namespace screen {

// Power, brightness, colour, effect and sync for the selected controller.
class ControlScreen : public BaseScreen {
public:
    ControlScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    bool handle_key(uint32_t key) override;

private:
    struct Row {
        lv_obj_t* row{nullptr};
        lv_obj_t* name{nullptr};
        lv_obj_t* value{nullptr};
        lv_obj_t* track{nullptr};
        lv_obj_t* fill{nullptr};
        lv_obj_t* swatch{nullptr};
    };

    void build_content(lv_obj_t* content) override;
    void refresh() override;
    Row create_row(const char* name, bool with_bar, bool with_swatch);
    void set_bar(Row& row, int value, int max, lv_color_t color);

    lv_obj_t* list_{nullptr};
    lv_obj_t* overlay_{nullptr};
    std::array<Row, static_cast<std::size_t>(viewmodel::ControlRow::Count)> rows_{};
};

} // namespace screen
