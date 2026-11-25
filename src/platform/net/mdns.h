/*
 * SPDX-License-Identifier: MIT
 *
 * Tiny mDNS/DNS-SD browser. WLED advertises "_wled._tcp" (with its MAC in a TXT record), so a
 * single PTR query is enough to find every controller on the subnet without avahi.
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace platform::net {

struct MdnsService {
    std::string instance;   // e.g. "wled-kitchen"
    std::string host;       // SRV target, e.g. "wled-kitchen.local"
    std::string ip;         // dotted IPv4
    uint16_t port{0};
    std::string mac;        // from TXT "mac=..."
};

// Builds a PTR query for service_fqdn (e.g. "_wled._tcp.local"). unicast_response sets the
// QU bit so responders may answer us directly.
std::vector<uint8_t> build_mdns_query(const std::string& service_fqdn,
                                      uint16_t transaction_id,
                                      bool unicast_response);

// Parses one mDNS response packet and returns the services of type service_fqdn it describes.
// source_ip is used when the packet has no A record for the SRV target.
bool parse_mdns_response(const uint8_t* data,
                         std::size_t length,
                         const std::string& service_fqdn,
                         const std::string& source_ip,
                         std::vector<MdnsService>& services);

using MdnsCallback = std::function<void(const MdnsService&)>;

// Queries the network for timeout_ms and calls on_found once per new service. Blocking; run it on
// a worker thread. Returns false only if no socket could be opened.
bool mdns_browse(const std::string& service_fqdn,
                 int timeout_ms,
                 const std::atomic<bool>& cancel,
                 const MdnsCallback& on_found,
                 std::string& error);

} // namespace platform::net
