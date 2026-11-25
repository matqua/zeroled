/*
 * SPDX-License-Identifier: MIT
 *
 * Desktop debugging tool that drives the same networking code as the app, without a UI.
 *
 *   zeroled_probe discover          mDNS + UDP + peer-list discovery, then poll each device
 *   zeroled_probe info <ip>         read /json/si, /json/eff and /json/nodes (read-only)
 *   zeroled_probe set <ip> <json>   POST a /json/state patch (changes the lights!)
 */

#include "wled_api.h"
#include "wled_service.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <thread>

namespace {

using platform::wled::EventKind;

void print_device(const std::string& ip, const model::WledState& s, const model::WledInfo& i) {
    std::printf("  %-15s %-20s v%-8s %3d LEDs  %s bri=%3d fx=%3d sx=%3d ix=%3d rgb=%3d,%3d,%3d  "
                "sync send=%d recv=%d rgrp=%d  %d mA  wifi %d%%\n",
                ip.c_str(), i.name.c_str(), i.version.c_str(), i.led_count, s.on ? "ON " : "off",
                s.bri, s.fx, s.sx, s.ix, s.color.r, s.color.g, s.color.b, s.sync_send, s.sync_recv,
                s.receive_groups, i.power_ma, i.wifi_signal);
}

int discover() {
    platform::wled::WledService service;
    service.start();
    service.discover();

    std::map<std::string, std::string> found;     // ip -> name/source
    std::map<std::string, bool> polled;
    const auto start = std::chrono::steady_clock::now();
    bool discovery_done = false;
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(8)) {
        for (auto& event : service.drain()) {
            switch (event.kind) {
                case EventKind::DeviceFound: {
                    const char* via = event.via == platform::wled::FoundVia::Mdns
                                          ? "mdns"
                                          : (event.via == platform::wled::FoundVia::UdpBroadcast ? "udp" : "peer");
                    if (found.emplace(event.ip, event.name).second) {
                        std::printf("found %-15s %-20s via %s %s\n", event.ip.c_str(), event.name.c_str(), via,
                                    event.mac.c_str());
                        service.poll(event.ip, 0);
                    }
                    break;
                }
                case EventKind::DiscoveryFinished:
                    discovery_done = true;
                    std::printf("mDNS browse finished%s%s\n", event.error.empty() ? "" : ": ", event.error.c_str());
                    break;
                case EventKind::PollResult:
                    if (polled.emplace(event.ip, event.ok).second) {
                        if (event.ok) {
                            print_device(event.ip, event.state, event.info);
                            service.fetch_peers(event.ip);
                        }
                        else {
                            std::printf("  %-15s poll failed: %s\n", event.ip.c_str(), event.error.c_str());
                        }
                    }
                    break;
                default:
                    break;
            }
        }
        if (discovery_done && polled.size() == found.size() && !found.empty() &&
            std::chrono::steady_clock::now() - start > std::chrono::seconds(4)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    service.stop();
    std::printf("%zu device(s) found, %zu answered\n", found.size(),
                static_cast<std::size_t>(std::count_if(polled.begin(), polled.end(),
                                                       [](const auto& p) { return p.second; })));
    return found.empty() ? 1 : 0;
}

int info(const std::string& ip) {
    model::WledState state;
    model::WledInfo device_info;
    std::string error;
    if (!platform::wled::fetch_state_info(ip, 2000, state, device_info, error)) {
        std::printf("si failed: %s\n", error.c_str());
        return 1;
    }
    print_device(ip, state, device_info);
    std::vector<std::string> effects;
    if (platform::wled::fetch_effects(ip, 4000, effects, error)) {
        std::printf("  %zu effects, first: %s, current: %s\n", effects.size(),
                    effects.empty() ? "-" : effects[0].c_str(),
                    state.fx < static_cast<int>(effects.size()) ? effects[static_cast<std::size_t>(state.fx)].c_str() : "?");
    }
    else {
        std::printf("  effects failed: %s\n", error.c_str());
    }
    std::vector<model::WledPeer> peers;
    if (platform::wled::fetch_nodes(ip, 2000, peers, error)) {
        std::printf("  %zu peers:", peers.size());
        for (const auto& peer : peers) {
            std::printf(" %s(%s)", peer.ip.c_str(), peer.name.c_str());
        }
        std::printf("\n");
    }
    return 0;
}

int set(const std::string& ip, const std::string& patch) {
    std::string error;
    if (!platform::wled::post_state(ip, patch, 2000, error)) {
        std::printf("post failed: %s\n", error.c_str());
        return 1;
    }
    std::printf("ok\n");
    return info(ip);
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && std::strcmp(argv[1], "discover") == 0) {
        return discover();
    }
    if (argc >= 3 && std::strcmp(argv[1], "info") == 0) {
        return info(argv[2]);
    }
    if (argc >= 4 && std::strcmp(argv[1], "set") == 0) {
        return set(argv[2], argv[3]);
    }
    std::fprintf(stderr, "usage: %s discover | info <ip> | set <ip> <json>\n", argv[0]);
    return 2;
}
