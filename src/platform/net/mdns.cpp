/*
 * SPDX-License-Identifier: MIT
 */

#include "mdns.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <iterator>
#include <map>
#include <set>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace platform::net {
namespace {

constexpr uint16_t kTypeA = 1;
constexpr uint16_t kTypePtr = 12;
constexpr uint16_t kTypeTxt = 16;
constexpr uint16_t kTypeSrv = 33;
constexpr uint16_t kClassIn = 1;
constexpr const char* kMdnsGroup = "224.0.0.251";
constexpr uint16_t kMdnsPort = 5353;

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string strip_trailing_dot(std::string value) {
    if (!value.empty() && value.back() == '.') {
        value.pop_back();
    }
    return value;
}

uint16_t read_u16(const uint8_t* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

// Reads a (possibly compressed) DNS name starting at offset. On success offset is advanced past
// the name in the original position.
bool read_name(const uint8_t* data, std::size_t length, std::size_t& offset, std::string& name) {
    name.clear();
    std::size_t pos = offset;
    bool jumped = false;
    int hops = 0;

    while (true) {
        if (pos >= length) {
            return false;
        }
        const uint8_t len = data[pos];
        if ((len & 0xC0) == 0xC0) {
            if (pos + 1 >= length || ++hops > 16) {
                return false;
            }
            const std::size_t target = static_cast<std::size_t>(((len & 0x3F) << 8) | data[pos + 1]);
            if (!jumped) {
                offset = pos + 2;
            }
            jumped = true;
            pos = target;
            continue;
        }
        if (len & 0xC0) {
            return false; // reserved label types
        }
        if (len == 0) {
            if (!jumped) {
                offset = pos + 1;
            }
            return true;
        }
        if (pos + 1 + len > length) {
            return false;
        }
        if (!name.empty()) {
            name += '.';
        }
        name.append(reinterpret_cast<const char*>(data + pos + 1), len);
        pos += 1 + len;
        if (name.size() > 255) {
            return false;
        }
    }
}

void append_name(std::vector<uint8_t>& out, const std::string& fqdn) {
    std::size_t start = 0;
    while (start <= fqdn.size()) {
        auto dot = fqdn.find('.', start);
        if (dot == std::string::npos) {
            dot = fqdn.size();
        }
        const auto label = fqdn.substr(start, dot - start);
        if (!label.empty()) {
            out.push_back(static_cast<uint8_t>(std::min<std::size_t>(label.size(), 63)));
            out.insert(out.end(), label.begin(), label.begin() + std::min<std::size_t>(label.size(), 63));
        }
        start = dot + 1;
    }
    out.push_back(0);
}

// The instance label is the first label of "wled-kitchen._wled._tcp.local".
std::string instance_label(const std::string& instance_fqdn, const std::string& service_fqdn) {
    const auto suffix = "." + lower(service_fqdn);
    const auto lowered = lower(instance_fqdn);
    if (lowered.size() > suffix.size() &&
        lowered.compare(lowered.size() - suffix.size(), suffix.size(), suffix) == 0) {
        return instance_fqdn.substr(0, instance_fqdn.size() - suffix.size());
    }
    return instance_fqdn;
}

int open_udp_socket() {
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return -1;
    }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#ifdef SO_REUSEPORT
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
#endif
    unsigned char ttl = 255;
    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
    unsigned char loop = 1;
    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));
    return fd;
}

} // namespace

std::vector<uint8_t> build_mdns_query(const std::string& service_fqdn,
                                      uint16_t transaction_id,
                                      bool unicast_response) {
    std::vector<uint8_t> packet;
    packet.reserve(64);
    packet.push_back(static_cast<uint8_t>(transaction_id >> 8));
    packet.push_back(static_cast<uint8_t>(transaction_id & 0xFF));
    packet.insert(packet.end(), {0x00, 0x00}); // flags: standard query
    packet.insert(packet.end(), {0x00, 0x01}); // QDCOUNT = 1
    packet.insert(packet.end(), {0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    append_name(packet, service_fqdn);
    packet.insert(packet.end(), {0x00, static_cast<uint8_t>(kTypePtr)});
    const uint16_t qclass = static_cast<uint16_t>(kClassIn | (unicast_response ? 0x8000 : 0));
    packet.push_back(static_cast<uint8_t>(qclass >> 8));
    packet.push_back(static_cast<uint8_t>(qclass & 0xFF));
    return packet;
}

bool parse_mdns_response(const uint8_t* data,
                         std::size_t length,
                         const std::string& service_fqdn,
                         const std::string& source_ip,
                         std::vector<MdnsService>& services) {
    if (length < 12) {
        return false;
    }
    const uint16_t flags = read_u16(data + 2);
    if ((flags & 0x8000) == 0) {
        return false; // a query, not a response
    }
    const uint16_t qdcount = read_u16(data + 4);
    const uint16_t record_count = static_cast<uint16_t>(read_u16(data + 6) + read_u16(data + 8) +
                                                        read_u16(data + 10));

    std::size_t offset = 12;
    std::string name;
    for (uint16_t i = 0; i < qdcount; ++i) {
        if (!read_name(data, length, offset, name) || offset + 4 > length) {
            return false;
        }
        offset += 4;
    }

    const auto service = lower(strip_trailing_dot(service_fqdn));
    std::vector<std::string> instances;                          // PTR targets
    std::map<std::string, std::pair<std::string, uint16_t>> srv; // instance -> host, port
    std::map<std::string, std::string> txt_mac;                  // instance -> mac
    std::map<std::string, std::string> a_records;                // host -> ip

    for (uint16_t i = 0; i < record_count; ++i) {
        if (!read_name(data, length, offset, name) || offset + 10 > length) {
            return false;
        }
        const uint16_t type = read_u16(data + offset);
        const uint16_t rdlength = read_u16(data + offset + 8);
        offset += 10;
        if (offset + rdlength > length) {
            return false;
        }
        const std::size_t rdata = offset;
        const auto owner = lower(name);

        if (type == kTypePtr && owner == service) {
            std::size_t p = rdata;
            std::string target;
            if (read_name(data, length, p, target)) {
                instances.push_back(target);
            }
        }
        else if (type == kTypeSrv && rdlength >= 7) {
            const uint16_t port = read_u16(data + rdata + 4);
            std::size_t p = rdata + 6;
            std::string target;
            if (read_name(data, length, p, target)) {
                srv[owner] = {lower(target), port};
            }
        }
        else if (type == kTypeTxt) {
            std::size_t p = rdata;
            while (p < rdata + rdlength) {
                const uint8_t len = data[p++];
                if (p + len > rdata + rdlength) {
                    break;
                }
                const std::string entry(reinterpret_cast<const char*>(data + p), len);
                if (lower(entry.substr(0, 4)) == "mac=") {
                    txt_mac[owner] = entry.substr(4);
                }
                p += len;
            }
        }
        else if (type == kTypeA && rdlength == 4) {
            char text[INET_ADDRSTRLEN] = {};
            inet_ntop(AF_INET, data + rdata, text, sizeof(text));
            a_records[owner] = text;
        }
        offset += rdlength;
    }

    // Some responders answer with only SRV/TXT/A (no PTR) when asked for a known instance.
    for (const auto& entry : srv) {
        const auto& owner = entry.first;
        if (owner.size() > service.size() + 1 &&
            owner.compare(owner.size() - service.size(), service.size(), service) == 0 &&
            std::none_of(instances.begin(), instances.end(),
                         [&](const std::string& inst) { return lower(inst) == owner; })) {
            instances.push_back(owner);
        }
    }

    bool found = false;
    for (const auto& instance_fqdn : instances) {
        MdnsService result;
        const auto key = lower(instance_fqdn);
        result.instance = instance_label(instance_fqdn, service);
        const auto srv_it = srv.find(key);
        if (srv_it != srv.end()) {
            result.host = strip_trailing_dot(srv_it->second.first);
            result.port = srv_it->second.second;
            const auto a_it = a_records.find(srv_it->second.first);
            if (a_it != a_records.end()) {
                result.ip = a_it->second;
            }
        }
        if (result.ip.empty() && a_records.size() == 1) {
            result.ip = a_records.begin()->second;
        }
        if (result.ip.empty()) {
            result.ip = source_ip;
        }
        const auto mac_it = txt_mac.find(key);
        if (mac_it != txt_mac.end()) {
            result.mac = mac_it->second;
        }
        if (!result.ip.empty()) {
            services.push_back(std::move(result));
            found = true;
        }
    }
    return found;
}

bool mdns_browse(const std::string& service_fqdn,
                 int timeout_ms,
                 const std::atomic<bool>& cancel,
                 const MdnsCallback& on_found,
                 std::string& error) {
    using Clock = std::chrono::steady_clock;
    error.clear();

    // Socket 1: ephemeral port. Queries from a port other than 5353 are "legacy unicast" queries
    // (RFC 6762 6.7) and responders reply straight back to this port.
    const int unicast_fd = open_udp_socket();
    // Socket 2: shared 5353 listener in the multicast group, for responders that only multicast.
    // Best effort: avahi/mDNSResponder may already own the port without SO_REUSEPORT.
    int group_fd = open_udp_socket();

    if (unicast_fd < 0 && group_fd < 0) {
        error = std::string("socket: ") + std::strerror(errno);
        return false;
    }

    if (group_fd >= 0) {
        sockaddr_in bind_addr{};
        bind_addr.sin_family = AF_INET;
        bind_addr.sin_port = htons(kMdnsPort);
        bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        ip_mreq membership{};
        inet_pton(AF_INET, kMdnsGroup, &membership.imr_multiaddr);
        membership.imr_interface.s_addr = htonl(INADDR_ANY);
        if (::bind(group_fd, reinterpret_cast<sockaddr*>(&bind_addr), sizeof(bind_addr)) != 0 ||
            setsockopt(group_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &membership, sizeof(membership)) != 0) {
            ::close(group_fd);
            group_fd = -1;
        }
    }

    sockaddr_in group{};
    group.sin_family = AF_INET;
    group.sin_port = htons(kMdnsPort);
    inet_pton(AF_INET, kMdnsGroup, &group.sin_addr);

    const auto send_query = [&](uint16_t id) {
        const int fd = unicast_fd >= 0 ? unicast_fd : group_fd;
        const auto packet = build_mdns_query(service_fqdn, id, true);
        ::sendto(fd, packet.data(), packet.size(), 0, reinterpret_cast<sockaddr*>(&group),
                 sizeof(group));
    };

    std::set<std::string> reported;
    const auto start = Clock::now();
    const auto deadline = start + std::chrono::milliseconds(timeout_ms);
    // Re-send a couple of times: multicast over Wi-Fi is lossy and ESP responders are slow.
    const int resend_at_ms[] = {0, 700, 1600};
    std::size_t next_send = 0;
    uint16_t query_id = 0; // mDNS queries use ID 0; legacy responders echo it

    uint8_t buffer[9000];
    while (!cancel.load() && Clock::now() < deadline) {
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
        if (next_send < std::size(resend_at_ms) && elapsed >= resend_at_ms[next_send]) {
            send_query(query_id);
            ++next_send;
        }

        pollfd fds[2];
        nfds_t count = 0;
        if (unicast_fd >= 0) fds[count++] = {unicast_fd, POLLIN, 0};
        if (group_fd >= 0) fds[count++] = {group_fd, POLLIN, 0};
        const int rc = ::poll(fds, count, 100);
        if (rc <= 0) {
            continue;
        }
        for (nfds_t i = 0; i < count; ++i) {
            if (!(fds[i].revents & POLLIN)) {
                continue;
            }
            sockaddr_in from{};
            socklen_t from_len = sizeof(from);
            const auto n = ::recvfrom(fds[i].fd, buffer, sizeof(buffer), 0,
                                      reinterpret_cast<sockaddr*>(&from), &from_len);
            if (n <= 0) {
                continue;
            }
            char source[INET_ADDRSTRLEN] = {};
            inet_ntop(AF_INET, &from.sin_addr, source, sizeof(source));

            std::vector<MdnsService> found;
            parse_mdns_response(buffer, static_cast<std::size_t>(n), service_fqdn, source, found);
            for (const auto& service : found) {
                if (reported.insert(service.ip).second) {
                    on_found(service);
                }
            }
        }
    }

    if (unicast_fd >= 0) ::close(unicast_fd);
    if (group_fd >= 0) ::close(group_fd);
    return true;
}

} // namespace platform::net
