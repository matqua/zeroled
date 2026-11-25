/*
 * SPDX-License-Identifier: MIT
 *
 * Plain data types describing a WLED controller. No LVGL or socket types live here so the
 * platform layer (which produces them) and the view model (which consumes them) can share them.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace model {

struct WledColor {
    uint8_t r{255};
    uint8_t g{255};
    uint8_t b{255};

    bool operator==(const WledColor& other) const {
        return r == other.r && g == other.g && b == other.b;
    }
    bool operator!=(const WledColor& other) const { return !(*this == other); }
};

// Subset of /json/state that the app reads and writes.
struct WledState {
    bool on{false};
    int bri{0};               // 0..255 master brightness
    WledColor color;          // primary colour of the main segment
    int fx{0};                // effect id of the main segment
    int sx{128};              // effect speed 0..255
    int ix{128};              // effect intensity 0..255
    int pal{0};               // palette id
    int main_segment{0};
    bool any_segment_selected{true};

    // UDP sync ("udpn"). WLED <= 0.14 accepts a writable "recv" flag; newer firmware derives
    // receive from the receive-group mask (rgrp != 0), so both are tracked.
    bool sync_send{false};
    bool sync_recv{false};
    int sync_groups{1};
    int receive_groups{1};
};

// Subset of /json/info shown on the info screen.
struct WledInfo {
    std::string name;
    std::string version;
    int version_id{0};
    std::string mac;
    std::string ip;
    std::string arch;
    std::string core;
    std::string brand;
    std::string product;

    int led_count{0};
    int power_ma{0};
    int max_power_ma{0};
    int fps{0};

    int wifi_rssi{0};
    int wifi_signal{0};
    int wifi_channel{0};
    std::string wifi_band;

    bool live{false};
    std::string live_mode;
    std::string live_ip;

    int ws_clients{-1};
    int effect_count{0};
    int palette_count{0};
    int fs_used_kb{0};
    int fs_total_kb{0};
    int node_count{-1};
    long long uptime_s{0};
    long long free_heap{0};
};

// A WLED peer reported by /json/nodes or a UDP node broadcast.
struct WledPeer {
    std::string ip;
    std::string name;
};

} // namespace model
