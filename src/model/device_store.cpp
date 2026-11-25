/*
 * SPDX-License-Identifier: MIT
 */

#include "device_store.h"

#include <algorithm>
#include <cctype>

namespace model {
namespace {

std::string lower(const std::string& value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

} // namespace

Device& DeviceStore::upsert(const std::string& ip,
                            const std::string& name_hint,
                            DeviceSource source,
                            bool* created) {
    if (auto* existing = find(ip)) {
        // A name from /json/info (set elsewhere once verified) wins over discovery hints.
        if (!existing->verified && existing->name.empty() && !name_hint.empty()) {
            existing->name = name_hint;
        }
        if (created) {
            *created = false;
        }
        return *existing;
    }

    Device device;
    device.ip = ip;
    device.name = name_hint;
    device.source = source;
    devices_.push_back(std::move(device));
    if (created) {
        *created = true;
    }
    return devices_.back();
}

Device* DeviceStore::find(const std::string& ip) {
    for (auto& device : devices_) {
        if (device.ip == ip) {
            return &device;
        }
    }
    return nullptr;
}

const Device* DeviceStore::find(const std::string& ip) const {
    for (const auto& device : devices_) {
        if (device.ip == ip) {
            return &device;
        }
    }
    return nullptr;
}

int DeviceStore::index_of(const std::string& ip) const {
    for (std::size_t i = 0; i < devices_.size(); ++i) {
        if (devices_[i].ip == ip) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool DeviceStore::remove(const std::string& ip) {
    const auto it = std::find_if(devices_.begin(), devices_.end(), [&](const Device& device) {
        return device.ip == ip;
    });
    if (it == devices_.end()) {
        return false;
    }
    devices_.erase(it);
    return true;
}

void DeviceStore::sort() {
    std::stable_sort(devices_.begin(), devices_.end(), [](const Device& a, const Device& b) {
        const auto an = lower(a.display_name());
        const auto bn = lower(b.display_name());
        if (an != bn) {
            return an < bn;
        }
        return a.ip < b.ip;
    });
}

const char* to_string(DeviceSource source) {
    switch (source) {
        case DeviceSource::Mdns:
            return "mDNS";
        case DeviceSource::UdpBroadcast:
            return "UDP";
        case DeviceSource::PeerList:
            return "peer";
        case DeviceSource::Manual:
            return "manual";
        case DeviceSource::Saved:
            return "saved";
    }
    return "?";
}

} // namespace model
