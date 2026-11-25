/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"

#include <string>
#include <vector>

namespace screen {

// Scrollable effect picker for the selected controller.
class EffectsScreen : public BaseScreen {
public:
    EffectsScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    bool handle_key(uint32_t key) override;

private:
    void build_content(lv_obj_t* content) override;
    void refresh() override;
    void rebuild_rows();

    lv_obj_t* list_{nullptr};
    lv_obj_t* empty_label_{nullptr};
    std::vector<lv_obj_t*> rows_;
    std::vector<int> row_ids_;
    std::string built_for_ip_;
};

} // namespace screen
