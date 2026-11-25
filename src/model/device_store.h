/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "wled_types.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace model {

enum class DeviceSource : uint8_t {
    Mdns,
    UdpBroadcast,
    PeerList,
    Manual,
    Saved,
};

struct Device {
    std::string ip;
    std::string name;   // best known display name (mDNS instance until /json/info answers)
    std::string mac;
    DeviceSource source{DeviceSource::Mdns};

    bool verified{false};   // answered /json/si at least once
    bool online{false};     // last poll succeeded
    int consecutive_failures{0};
    std::string last_error;

    bool has_state{false};
    WledState state;
    WledInfo info;

    std::vector<std::string> effects;   // index == effect id
    bool effects_loaded{false};
    bool effects_requested{false};

    // Bumped on every local change. A poll result is only applied when its generation still
    // matches, so a reply that left the device before our command cannot undo it on screen.
    uint32_t generation{0};
    uint32_t next_poll_ms{0};
    bool poll_in_flight{false};

    // Colour editing works in HSV; keep the last hue/saturation the user chose so 8-bit RGB
    // round trips don't make the hue drift (or vanish for white).
    WledColor hsv_anchor_color;
    float hue_deg{0.0F};
    float saturation{1.0F};
    bool hsv_anchor_valid{false};

    // Group masks to restore when sync is re-enabled (newer firmware: rgrp == 0 means off).
    int saved_receive_groups{1};
    int saved_sync_groups{1};

    const std::string& display_name() const { return name.empty() ? ip : name; }
};

class DeviceStore {
public:
    // Returns the device for ip, creating it if needed. created is set when the device is new.
    Device& upsert(const std::string& ip, const std::string& name_hint, DeviceSource source,
                   bool* created = nullptr);

    Device* find(const std::string& ip);
    const Device* find(const std::string& ip) const;
    int index_of(const std::string& ip) const;

    std::vector<Device>& devices() { return devices_; }
    const std::vector<Device>& devices() const { return devices_; }
    std::size_t size() const { return devices_.size(); }
    bool empty() const { return devices_.empty(); }

    bool remove(const std::string& ip);

    // Keeps the list ordered by display name (case-insensitive), then IP.
    void sort();

private:
    std::vector<Device> devices_;
};

const char* to_string(DeviceSource source);

} // namespace model
