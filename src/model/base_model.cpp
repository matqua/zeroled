/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "base_model.h"

namespace model {

bool BaseModel::dark_mode() const {
    return dark_mode_;
}

void BaseModel::set_dark_mode(bool enabled) {
    dark_mode_ = enabled;
}

void BaseModel::toggle_dark_mode() {
    dark_mode_ = !dark_mode_;
}

AppPage BaseModel::current_page() const {
    return current_page_;
}

void BaseModel::set_current_page(AppPage page) {
    current_page_ = page;
}

const char* page_name(AppPage page) {
    switch (page) {
        case AppPage::Devices:
            return "DEVICES";
        case AppPage::Control:
            return "CONTROL";
        case AppPage::Effects:
            return "EFFECTS";
        case AppPage::Info:
            return "INFO";
        case AppPage::AddDevice:
            return "ADD DEVICE";
    }
    return "";
}

} // namespace model
