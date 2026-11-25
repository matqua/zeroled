/*
 * SPDX-License-Identifier: MIT
 *
 * WLED JSON API (https://kno.wled.ge/interfaces/json-api/): parsing of /json/si, /json/eff and
 * /json/nodes, builders for /json/state patches, and blocking request helpers.
 */

#pragma once

#include "wled_types.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace platform::wled {

// ---- parsing (pure, unit tested) ----
bool parse_state_info(const std::string& body,
                      model::WledState& state,
                      model::WledInfo& info,
                      std::string& error);
bool parse_effects(const std::string& body, std::vector<std::string>& effects, std::string& error);
bool parse_nodes(const std::string& body, std::vector<model::WledPeer>& peers, std::string& error);

// Parses a 44-byte UDP node broadcast (port 65506). Returns false for other packets.
bool parse_node_broadcast(const uint8_t* data, std::size_t length, model::WledPeer& peer);

// ---- /json/state patch builders ----
std::string patch_power(bool on);
std::string patch_brightness(int bri);
// Segment-level fields target all selected segments, or main_segment when none are selected.
std::string patch_color(const model::WledState& state, model::WledColor color);
std::string patch_effect(const model::WledState& state, int fx);
std::string patch_speed(const model::WledState& state, int sx);
std::string patch_intensity(const model::WledState& state, int ix);
// Enabling send also makes sure at least one send group is set (send_groups, else 1); enabling
// receive restores the receive-group mask (receive_groups, else 1), since newer firmware treats
// rgrp == 0 as "receive off".
std::string patch_sync(bool send, bool receive, int send_groups, int receive_groups);

// ---- blocking requests (worker threads only) ----
bool fetch_state_info(const std::string& ip, int timeout_ms, model::WledState& state,
                      model::WledInfo& info, std::string& error);
bool fetch_effects(const std::string& ip, int timeout_ms, std::vector<std::string>& effects,
                   std::string& error);
bool fetch_nodes(const std::string& ip, int timeout_ms, std::vector<model::WledPeer>& peers,
                 std::string& error);
bool post_state(const std::string& ip, const std::string& patch, int timeout_ms, std::string& error);

// Effect names WLED uses as placeholders for removed/reserved ids.
bool is_placeholder_effect(const std::string& name);

} // namespace platform::wled
