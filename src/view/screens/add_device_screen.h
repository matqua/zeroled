/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"

namespace screen {

// Manual IPv4 entry for networks where mDNS/UDP discovery does not reach the controllers.
class AddDeviceScreen : public BaseScreen {
public:
    AddDeviceScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    bool handle_key(uint32_t key) override;

private:
    void build_content(lv_obj_t* content) override;
    void refresh() override;

    lv_obj_t* prompt_{nullptr};
    lv_obj_t* field_{nullptr};
    lv_obj_t* hint_{nullptr};
};

} // namespace screen
