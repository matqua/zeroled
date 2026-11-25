/*
 * SPDX-License-Identifier: MIT
 */

#include "info_screen.h"

#include "device_store.h"
#include "input_keys.h"

#include <cstdio>
#include <string>

namespace screen {
namespace {

constexpr int32_t kRowHeight = 16;
constexpr std::size_t kRowCount = 16;

std::string format_uptime(long long seconds) {
    const long long days = seconds / 86400;
    const long long hours = (seconds % 86400) / 3600;
    const long long minutes = (seconds % 3600) / 60;
    char text[32];
    if (days > 0) {
        std::snprintf(text, sizeof(text), "%lldd %lldh %lldm", days, hours, minutes);
    }
    else if (hours > 0) {
        std::snprintf(text, sizeof(text), "%lldh %lldm", hours, minutes);
    }
    else {
        std::snprintf(text, sizeof(text), "%lldm %llds", minutes, seconds % 60);
    }
    return text;
}

std::string on_off(bool value) {
    return value ? "on" : "off";
}

} // namespace

InfoScreen::InfoScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

void InfoScreen::build_content(lv_obj_t* content) {
    list_ = lv_obj_create(content);
    lv_obj_remove_style_all(list_);
    lv_obj_set_size(list_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(list_, 8, 0);
    lv_obj_set_style_pad_ver(list_, 3, 0);
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_ACTIVE);

    const auto* key_font = font(12, true);
    const auto* value_font = font(12);
    for (std::size_t i = 0; i < kRowCount; ++i) {
        auto* row = lv_obj_create(list_);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), kRowHeight);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        Row widgets;
        widgets.key = lv_label_create(row);
        lv_obj_set_width(widgets.key, 84);
        lv_obj_set_style_text_font(widgets.key, key_font, 0);
        lv_obj_align(widgets.key, LV_ALIGN_LEFT_MID, 0, 0);

        widgets.value = lv_label_create(row);
        lv_obj_set_width(widgets.value, 216);
        lv_label_set_long_mode(widgets.value, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(widgets.value, value_font, 0);
        lv_obj_align(widgets.value, LV_ALIGN_LEFT_MID, 86, 0);
        rows_.push_back(widgets);
    }
}

void InfoScreen::set_row(std::size_t index, const char* key, const std::string& value,
                         lv_color_t color) {
    if (index >= rows_.size()) {
        return;
    }
    const auto palette = colors();
    lv_label_set_text(rows_[index].key, key);
    lv_obj_set_style_text_color(rows_[index].key, palette.primary, 0);
    lv_label_set_text(rows_[index].value, value.c_str());
    lv_obj_set_style_text_color(rows_[index].value, color, 0);
}

void InfoScreen::refresh() {
    const auto palette = colors();
    const auto* device = view_model().selected_device();
    if (!device) {
        return;
    }
    const auto& info = device->info;
    const auto& state = device->state;
    char text[96];
    std::size_t row = 0;
    const auto text_color = palette.text;

    const bool known = device->verified;
    const auto or_dash = [&](const std::string& value) { return known && !value.empty() ? value : std::string("-"); };

    std::snprintf(text, sizeof(text), "%s", device->online ? "online" : (known ? "offline" : "connecting"));
    std::string status = text;
    if (!device->online && !device->last_error.empty()) {
        status += " (" + device->last_error + ")";
    }
    set_row(row++, "Status", status, device->online ? palette.success : palette.error);
    set_row(row++, "IP", device->ip, text_color);
    set_row(row++, "MAC", or_dash(info.mac.empty() ? device->mac : info.mac), text_color);

    std::snprintf(text, sizeof(text), "%s  (build %d)", or_dash(info.version).c_str(), info.version_id);
    set_row(row++, "Version", known ? text : "-", text_color);
    std::snprintf(text, sizeof(text), "%s %s", info.arch.c_str(), info.core.c_str());
    set_row(row++, "Chip", known ? text : "-", text_color);

    std::snprintf(text, sizeof(text), "%d LEDs  @ %d fps", info.led_count, info.fps);
    set_row(row++, "LEDs", known ? text : "-", text_color);

    if (info.max_power_ma > 0) {
        std::snprintf(text, sizeof(text), "%d mA of %d mA (%.1f W @5V)", info.power_ma, info.max_power_ma,
                      info.power_ma * 5.0 / 1000.0);
    }
    else {
        std::snprintf(text, sizeof(text), "%d mA (limiter off)", info.power_ma);
    }
    set_row(row++, "Current", known ? text : "-", text_color);

    std::snprintf(text, sizeof(text), "%d%%  %d dBm  ch %d %s", info.wifi_signal, info.wifi_rssi,
                  info.wifi_channel, info.wifi_band.c_str());
    lv_color_t wifi_color = text_color;
    if (known && info.wifi_signal < 40) {
        wifi_color = info.wifi_signal < 20 ? palette.error : palette.warning;
    }
    set_row(row++, "Wi-Fi", known ? text : "-", wifi_color);

    set_row(row++, "Uptime", known ? format_uptime(info.uptime_s) : "-", text_color);
    std::snprintf(text, sizeof(text), "%lld KB", info.free_heap / 1024);
    set_row(row++, "Free heap", known ? text : "-", text_color);

    std::snprintf(text, sizeof(text), "send %s  recv %s  (grp %d/%d)", on_off(state.sync_send).c_str(),
                  on_off(state.sync_recv).c_str(), state.sync_groups, state.receive_groups);
    set_row(row++, "UDP sync", device->has_state ? text : "-", text_color);

    if (info.live) {
        std::snprintf(text, sizeof(text), "%s from %s",
                      info.live_mode.empty() ? "realtime" : info.live_mode.c_str(),
                      info.live_ip.empty() ? "?" : info.live_ip.c_str());
        set_row(row++, "Live", text, palette.warning);
    }
    else {
        set_row(row++, "Live", known ? "no" : "-", text_color);
    }

    std::snprintf(text, sizeof(text), "%d effects, %d palettes", info.effect_count, info.palette_count);
    set_row(row++, "Library", known ? text : "-", text_color);
    std::snprintf(text, sizeof(text), "%d / %d KB", info.fs_used_kb, info.fs_total_kb);
    set_row(row++, "Storage", known ? text : "-", text_color);
    std::snprintf(text, sizeof(text), "%d websocket, %d peers", info.ws_clients < 0 ? 0 : info.ws_clients,
                  info.node_count < 0 ? 0 : info.node_count);
    set_row(row++, "Clients", known ? text : "-", text_color);
    set_row(row++, "Found via", model::to_string(device->source), text_color);
}

bool InfoScreen::handle_key(uint32_t key) {
    switch (view::classify_key(key)) {
        case view::NavKey::Up:
            lv_obj_scroll_by_bounded(list_, 0, kRowHeight * 3, LV_ANIM_ON);
            return true;
        case view::NavKey::Down:
            lv_obj_scroll_by_bounded(list_, 0, -kRowHeight * 3, LV_ANIM_ON);
            return true;
        case view::NavKey::Space:
            view_model().toggle_power();
            return true;
        default:
            return false;
    }
}

} // namespace screen
