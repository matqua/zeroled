/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"

#include <string>
#include <vector>

namespace screen {

// Metrics from /json/info for the selected controller.
class InfoScreen : public BaseScreen {
public:
    InfoScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    bool handle_key(uint32_t key) override;

private:
    struct Row {
        lv_obj_t* key{nullptr};
        lv_obj_t* value{nullptr};
    };

    void build_content(lv_obj_t* content) override;
    void refresh() override;
    void set_row(std::size_t index, const char* key, const std::string& value, lv_color_t color);

    lv_obj_t* list_{nullptr};
    std::vector<Row> rows_;
};

} // namespace screen
