/*
 * SPDX-License-Identifier: MIT
 */

#include "wled_api.h"

#include "http_client.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace platform::wled {
namespace {

using nlohmann::json;

template <typename T>
T value_or(const json& object, const char* key, T fallback) {
    if (!object.is_object()) {
        return fallback;
    }
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) {
        return fallback;
    }
    try {
        return it->get<T>();
    }
    catch (const json::exception&) {
        return fallback;
    }
}

// bool fields are sometimes sent as 0/1.
bool bool_or(const json& object, const char* key, bool fallback) {
    if (!object.is_object()) {
        return fallback;
    }
    const auto it = object.find(key);
    if (it == object.end()) {
        return fallback;
    }
    if (it->is_boolean()) {
        return it->get<bool>();
    }
    if (it->is_number()) {
        return it->get<double>() != 0.0;
    }
    return fallback;
}

int clamp_byte(int value) {
    return std::clamp(value, 0, 255);
}

bool parse_color(const json& value, model::WledColor& color) {
    if (value.is_array() && value.size() >= 3 && value[0].is_number()) {
        color.r = static_cast<uint8_t>(clamp_byte(value[0].get<int>()));
        color.g = static_cast<uint8_t>(clamp_byte(value[1].get<int>()));
        color.b = static_cast<uint8_t>(clamp_byte(value[2].get<int>()));
        return true;
    }
    if (value.is_string()) { // "RRGGBB" hex form
        const auto text = value.get<std::string>();
        unsigned int rgb = 0;
        if (std::sscanf(text.c_str(), "%6x", &rgb) == 1) {
            color.r = static_cast<uint8_t>((rgb >> 16) & 0xFF);
            color.g = static_cast<uint8_t>((rgb >> 8) & 0xFF);
            color.b = static_cast<uint8_t>(rgb & 0xFF);
            return true;
        }
    }
    return false;
}

void parse_state(const json& root, model::WledState& state) {
    state.on = bool_or(root, "on", state.on);
    state.bri = clamp_byte(value_or<int>(root, "bri", state.bri));
    state.main_segment = value_or<int>(root, "mainseg", 0);

    const auto udpn = root.find("udpn");
    if (udpn != root.end() && udpn->is_object()) {
        state.sync_send = bool_or(*udpn, "send", false);
        state.sync_groups = value_or<int>(*udpn, "sgrp", 1);
        state.receive_groups = value_or<int>(*udpn, "rgrp", 1);
        state.sync_recv = bool_or(*udpn, "recv", state.receive_groups != 0);
    }

    const auto segs = root.find("seg");
    if (segs == root.end() || !segs->is_array() || segs->empty()) {
        return;
    }

    const json* main = nullptr;
    state.any_segment_selected = false;
    for (const auto& seg : *segs) {
        if (!seg.is_object()) {
            continue;
        }
        if (bool_or(seg, "sel", false)) {
            state.any_segment_selected = true;
        }
        if (value_or<int>(seg, "id", -1) == state.main_segment) {
            main = &seg;
        }
    }
    if (!main) {
        main = &(*segs)[0];
    }

    state.fx = value_or<int>(*main, "fx", 0);
    state.sx = clamp_byte(value_or<int>(*main, "sx", 128));
    state.ix = clamp_byte(value_or<int>(*main, "ix", 128));
    state.pal = value_or<int>(*main, "pal", 0);
    const auto cols = main->find("col");
    if (cols != main->end() && cols->is_array() && !cols->empty()) {
        parse_color((*cols)[0], state.color);
    }
}

void parse_info(const json& root, model::WledInfo& info) {
    info.name = value_or<std::string>(root, "name", "");
    info.version = value_or<std::string>(root, "ver", "");
    info.version_id = value_or<int>(root, "vid", 0);
    info.mac = value_or<std::string>(root, "mac", "");
    info.ip = value_or<std::string>(root, "ip", "");
    info.arch = value_or<std::string>(root, "arch", "");
    info.core = value_or<std::string>(root, "core", "");
    info.brand = value_or<std::string>(root, "brand", "");
    info.product = value_or<std::string>(root, "product", "");
    info.live = bool_or(root, "live", false);
    info.live_mode = value_or<std::string>(root, "lm", "");
    info.live_ip = value_or<std::string>(root, "lip", "");
    info.ws_clients = value_or<int>(root, "ws", -1);
    info.effect_count = value_or<int>(root, "fxcount", 0);
    info.palette_count = value_or<int>(root, "palcount", 0);
    info.node_count = value_or<int>(root, "ndc", -1);
    info.uptime_s = value_or<long long>(root, "uptime", 0);
    info.free_heap = value_or<long long>(root, "freeheap", 0);

    const auto leds = root.find("leds");
    if (leds != root.end() && leds->is_object()) {
        info.led_count = value_or<int>(*leds, "count", 0);
        info.power_ma = value_or<int>(*leds, "pwr", 0);
        info.max_power_ma = value_or<int>(*leds, "maxpwr", 0);
        info.fps = value_or<int>(*leds, "fps", 0);
    }
    const auto wifi = root.find("wifi");
    if (wifi != root.end() && wifi->is_object()) {
        info.wifi_rssi = value_or<int>(*wifi, "rssi", 0);
        info.wifi_signal = value_or<int>(*wifi, "signal", 0);
        info.wifi_channel = value_or<int>(*wifi, "channel", 0);
        info.wifi_band = value_or<std::string>(*wifi, "band", "");
    }
    const auto fs = root.find("fs");
    if (fs != root.end() && fs->is_object()) {
        info.fs_used_kb = value_or<int>(*fs, "u", 0);
        info.fs_total_kb = value_or<int>(*fs, "t", 0);
    }
}

json segment_patch(const model::WledState& state, json fields) {
    // An object (not array) "seg" applies to every selected segment. If nothing is selected,
    // address the main segment explicitly so the change is not silently dropped.
    if (!state.any_segment_selected) {
        fields["id"] = state.main_segment;
    }
    return json{{"seg", std::move(fields)}};
}

bool parse_json(const std::string& body, json& out, std::string& error) {
    out = json::parse(body, nullptr, false);
    if (out.is_discarded()) {
        error = "invalid JSON";
        return false;
    }
    return true;
}

} // namespace

bool parse_state_info(const std::string& body,
                      model::WledState& state,
                      model::WledInfo& info,
                      std::string& error) {
    json root;
    if (!parse_json(body, root, error)) {
        return false;
    }
    if (!root.is_object()) {
        error = "unexpected JSON shape";
        return false;
    }
    const auto state_it = root.find("state");
    const auto info_it = root.find("info");
    if (state_it == root.end() || info_it == root.end() || !state_it->is_object() ||
        !info_it->is_object()) {
        error = "not a WLED /json/si response";
        return false;
    }
    // Identify WLED: every build reports a version string and an LED block.
    if (!info_it->contains("ver") || !info_it->contains("leds")) {
        error = "device does not look like WLED";
        return false;
    }
    parse_state(*state_it, state);
    parse_info(*info_it, info);
    return true;
}

bool parse_effects(const std::string& body, std::vector<std::string>& effects, std::string& error) {
    json root;
    if (!parse_json(body, root, error)) {
        return false;
    }
    if (!root.is_array()) {
        error = "effects list is not an array";
        return false;
    }
    effects.clear();
    effects.reserve(root.size());
    for (const auto& entry : root) {
        effects.push_back(entry.is_string() ? entry.get<std::string>() : std::string());
    }
    return true;
}

bool parse_nodes(const std::string& body, std::vector<model::WledPeer>& peers, std::string& error) {
    json root;
    if (!parse_json(body, root, error)) {
        return false;
    }
    const auto nodes = root.find("nodes");
    if (!root.is_object() || nodes == root.end() || !nodes->is_array()) {
        error = "no nodes array";
        return false;
    }
    peers.clear();
    for (const auto& node : *nodes) {
        model::WledPeer peer;
        peer.ip = value_or<std::string>(node, "ip", "");
        peer.name = value_or<std::string>(node, "name", "");
        if (net::is_ipv4_address(peer.ip)) {
            peers.push_back(std::move(peer));
        }
    }
    return true;
}

bool parse_node_broadcast(const uint8_t* data, std::size_t length, model::WledPeer& peer) {
    // Layout from WLED udp.cpp sendSysInfoUDP(): [0]=255 token, [1]=1 id, [2..5] IPv4,
    // [6..37] name (32 bytes, NUL padded), [38] node type, [39] unit id, [40..43] build.
    if (length < 40 || data[0] != 255 || data[1] != 1) {
        return false;
    }
    char ip[16];
    std::snprintf(ip, sizeof(ip), "%u.%u.%u.%u", data[2], data[3], data[4], data[5]);
    peer.ip = ip;
    std::string name(reinterpret_cast<const char*>(data + 6), 32);
    name.erase(std::find(name.begin(), name.end(), '\0'), name.end());
    name.erase(name.find_last_not_of(" \t") + 1);
    name.erase(0, name.find_first_not_of(" \t"));
    peer.name = name;
    return net::is_ipv4_address(peer.ip) && peer.ip != "0.0.0.0";
}

std::string patch_power(bool on) {
    return json{{"on", on}}.dump();
}

std::string patch_brightness(int bri) {
    return json{{"bri", clamp_byte(bri)}}.dump();
}

std::string patch_color(const model::WledState& state, model::WledColor color) {
    return segment_patch(state, json{{"col", json::array({json::array({color.r, color.g, color.b})})}}).dump();
}

std::string patch_effect(const model::WledState& state, int fx) {
    return segment_patch(state, json{{"fx", std::max(0, fx)}}).dump();
}

std::string patch_speed(const model::WledState& state, int sx) {
    return segment_patch(state, json{{"sx", clamp_byte(sx)}}).dump();
}

std::string patch_intensity(const model::WledState& state, int ix) {
    return segment_patch(state, json{{"ix", clamp_byte(ix)}}).dump();
}

std::string patch_sync(bool send, bool receive, int send_groups, int receive_groups) {
    // "recv" is honoured by WLED <= 0.14; newer builds ignore it and use the rgrp mask.
    json udpn{{"send", send}, {"recv", receive}, {"rgrp", receive ? std::max(1, receive_groups) : 0}};
    if (send) {
        udpn["sgrp"] = std::max(1, send_groups);
    }
    return json{{"udpn", std::move(udpn)}}.dump();
}

bool is_placeholder_effect(const std::string& name) {
    return name.empty() || name == "RSVD" || name == "-";
}

bool fetch_state_info(const std::string& ip, int timeout_ms, model::WledState& state,
                      model::WledInfo& info, std::string& error) {
    net::HttpResponse response;
    if (!net::http_get(ip, "/json/si", timeout_ms, response, error)) {
        return false;
    }
    if (response.status != 200) {
        error = "HTTP " + std::to_string(response.status);
        return false;
    }
    return parse_state_info(response.body, state, info, error);
}

bool fetch_effects(const std::string& ip, int timeout_ms, std::vector<std::string>& effects,
                   std::string& error) {
    net::HttpResponse response;
    if (!net::http_get(ip, "/json/eff", timeout_ms, response, error)) {
        return false;
    }
    if (response.status != 200) {
        error = "HTTP " + std::to_string(response.status);
        return false;
    }
    return parse_effects(response.body, effects, error);
}

bool fetch_nodes(const std::string& ip, int timeout_ms, std::vector<model::WledPeer>& peers,
                 std::string& error) {
    net::HttpResponse response;
    if (!net::http_get(ip, "/json/nodes", timeout_ms, response, error)) {
        return false;
    }
    if (response.status != 200) {
        error = "HTTP " + std::to_string(response.status);
        return false;
    }
    return parse_nodes(response.body, peers, error);
}

bool post_state(const std::string& ip, const std::string& patch, int timeout_ms, std::string& error) {
    net::HttpResponse response;
    if (!net::http_post_json(ip, "/json/state", patch, timeout_ms, response, error)) {
        return false;
    }
    if (response.status != 200) {
        error = "HTTP " + std::to_string(response.status);
        return false;
    }
    json root = json::parse(response.body, nullptr, false);
    if (root.is_object() && root.contains("error")) {
        error = "WLED error " + root["error"].dump();
        return false;
    }
    return true;
}

} // namespace platform::wled
