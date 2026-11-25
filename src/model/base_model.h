/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

namespace model {

enum class AppPage {
    Devices   = 0,   // discovered controller list (home)
    Control   = 1,   // power / brightness / colour / effect / sync for one controller
    Effects   = 2,   // effect picker
    Info      = 3,   // metrics for one controller
    AddDevice = 4,   // manual IP entry
};

class BaseModel {
public:
    bool dark_mode() const;
    void set_dark_mode(bool enabled);
    void toggle_dark_mode();

    AppPage current_page() const;
    void set_current_page(AppPage page);

private:
    bool dark_mode_ = true;
    AppPage current_page_ = AppPage::Devices;
};

const char* page_name(AppPage page);

} // namespace model
