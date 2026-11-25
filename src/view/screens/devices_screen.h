/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"

#include <vector>

namespace screen {

// Home page: every WLED controller found on the network.
class DevicesScreen : public BaseScreen {
public:
    DevicesScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    bool handle_key(uint32_t key) override;

private:
    struct Row {
        lv_obj_t* row{nullptr};
        lv_obj_t* swatch{nullptr};
        lv_obj_t* name{nullptr};
        lv_obj_t* ip{nullptr};
        lv_obj_t* status{nullptr};
    };

    void build_content(lv_obj_t* content) override;
    void refresh() override;
    Row create_row();

    lv_obj_t* list_{nullptr};
    lv_obj_t* empty_label_{nullptr};
    std::vector<Row> rows_;
};

} // namespace screen
