/*
 * SPDX-License-Identifier: MIT
 *
 * Host unit tests for the parts of ZeroLED that do not need a display or a network:
 * HTTP response parsing, mDNS packet parsing, WLED JSON parsing/patches and colour maths.
 */

#include "color.h"
#include "device_store.h"
#include "http_client.h"
#include "mdns.h"
#include "wled_api.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        ++g_checks;                                                                 \
        if (!(cond)) {                                                              \
            ++g_failures;                                                           \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                           \
    } while (0)

#define CHECK_EQ(a, b) CHECK((a) == (b))

// ---- HTTP ----

void test_http_content_length() {
    platform::net::HttpResponse response;
    std::string error;
    const std::string raw =
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 11\r\n\r\n{\"on\":true}";
    CHECK(platform::net::parse_http_response(raw, response, error));
    CHECK_EQ(response.status, 200);
    CHECK_EQ(response.body, std::string("{\"on\":true}"));
}

void test_http_chunked() {
    platform::net::HttpResponse response;
    std::string error;
    const std::string raw =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        "4\r\n[\"So\r\n6\r\nlid\",1\r\n1\r\n]\r\n0\r\n\r\n";
    CHECK(platform::net::parse_http_response(raw, response, error));
    CHECK_EQ(response.body, std::string("[\"Solid\",1]"));
}

void test_http_errors() {
    platform::net::HttpResponse response;
    std::string error;
    CHECK(!platform::net::parse_http_response("", response, error));
    CHECK(!platform::net::parse_http_response("HTTP/1.1 200 OK\r\nContent-Length: 50\r\n\r\nshort",
                                              response, error));
    CHECK(platform::net::parse_http_response("HTTP/1.1 404 Not Found\r\n\r\n", response, error));
    CHECK_EQ(response.status, 404);
    CHECK(platform::net::is_ipv4_address("192.168.1.20"));
    CHECK(!platform::net::is_ipv4_address("192.168.1"));
    CHECK(!platform::net::is_ipv4_address("wled.local"));
    std::string host;
    uint16_t port = 0;
    CHECK(platform::net::split_address("10.0.0.5", host, port) && host == "10.0.0.5" && port == 80);
    CHECK(platform::net::split_address("10.0.0.5:8080", host, port) && port == 8080);
    CHECK(!platform::net::is_device_address("10.0.0.5:"));
    CHECK(!platform::net::is_device_address("10.0.0.5:99999"));
    CHECK(!platform::net::is_device_address("10.0.0.5:80a"));
}

// ---- mDNS ----

void put_u16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

void put_u32(std::vector<uint8_t>& out, uint32_t value) {
    put_u16(out, static_cast<uint16_t>(value >> 16));
    put_u16(out, static_cast<uint16_t>(value & 0xFFFF));
}

void put_name(std::vector<uint8_t>& out, const std::string& name) {
    std::size_t start = 0;
    while (start < name.size()) {
        auto dot = name.find('.', start);
        if (dot == std::string::npos) dot = name.size();
        out.push_back(static_cast<uint8_t>(dot - start));
        out.insert(out.end(), name.begin() + static_cast<long>(start), name.begin() + static_cast<long>(dot));
        start = dot + 1;
    }
    out.push_back(0);
}

void put_record_header(std::vector<uint8_t>& out, uint16_t type, uint16_t rdlength) {
    put_u16(out, type);
    put_u16(out, 0x8001); // cache-flush + IN
    put_u32(out, 120);
    put_u16(out, rdlength);
}

// Builds a response shaped like ESP mDNS output: PTR answer, SRV/TXT/A additionals, using a
// compression pointer for the service name inside the PTR target.
std::vector<uint8_t> sample_mdns_response() {
    std::vector<uint8_t> p;
    put_u16(p, 0);      // id
    put_u16(p, 0x8400); // response, authoritative
    put_u16(p, 0);      // qd
    put_u16(p, 1);      // an
    put_u16(p, 0);      // ns
    put_u16(p, 3);      // ar

    const std::size_t service_offset = p.size();
    put_name(p, "_wled._tcp.local");
    // PTR -> "wled-desk" + pointer to service name
    std::vector<uint8_t> ptr_rdata;
    ptr_rdata.push_back(9);
    const std::string label = "wled-desk";
    ptr_rdata.insert(ptr_rdata.end(), label.begin(), label.end());
    ptr_rdata.push_back(static_cast<uint8_t>(0xC0 | (service_offset >> 8)));
    ptr_rdata.push_back(static_cast<uint8_t>(service_offset & 0xFF));
    put_record_header(p, 12, static_cast<uint16_t>(ptr_rdata.size()));
    p.insert(p.end(), ptr_rdata.begin(), ptr_rdata.end());

    // SRV for wled-desk._wled._tcp.local -> wled-desk.local:80
    put_name(p, "wled-desk._wled._tcp.local");
    std::vector<uint8_t> srv;
    put_u16(srv, 0);
    put_u16(srv, 0);
    put_u16(srv, 80);
    put_name(srv, "wled-desk.local");
    put_record_header(p, 33, static_cast<uint16_t>(srv.size()));
    p.insert(p.end(), srv.begin(), srv.end());

    // TXT mac=aabbccddeeff
    put_name(p, "wled-desk._wled._tcp.local");
    const std::string txt = "mac=aabbccddeeff";
    put_record_header(p, 16, static_cast<uint16_t>(txt.size() + 1));
    p.push_back(static_cast<uint8_t>(txt.size()));
    p.insert(p.end(), txt.begin(), txt.end());

    // A wled-desk.local -> 10.0.0.42
    put_name(p, "wled-desk.local");
    put_record_header(p, 1, 4);
    p.insert(p.end(), {10, 0, 0, 42});
    return p;
}

void test_mdns_parse() {
    const auto packet = sample_mdns_response();
    std::vector<platform::net::MdnsService> services;
    CHECK(platform::net::parse_mdns_response(packet.data(), packet.size(), "_wled._tcp.local",
                                             "10.0.0.99", services));
    CHECK_EQ(services.size(), 1u);
    if (!services.empty()) {
        CHECK_EQ(services[0].instance, std::string("wled-desk"));
        CHECK_EQ(services[0].ip, std::string("10.0.0.42")); // A record wins over source address
        CHECK_EQ(services[0].port, 80);
        CHECK_EQ(services[0].mac, std::string("aabbccddeeff"));
        CHECK_EQ(services[0].host, std::string("wled-desk.local"));
    }

    // Other service types are ignored.
    services.clear();
    CHECK(!platform::net::parse_mdns_response(packet.data(), packet.size(), "_http._tcp.local",
                                              "10.0.0.99", services));

    // Truncated packets must not crash or report anything.
    for (std::size_t cut = 0; cut < packet.size(); cut += 7) {
        services.clear();
        platform::net::parse_mdns_response(packet.data(), cut, "_wled._tcp.local", "10.0.0.99", services);
    }
    CHECK(true);
}

void test_mdns_query() {
    const auto query = platform::net::build_mdns_query("_wled._tcp.local", 0, true);
    // header(12) + name(1+5+1+4+1+5+1) + type(2) + class(2)
    CHECK_EQ(query.size(), 12u + 18u + 4u);
    CHECK_EQ(query[5], 1); // QDCOUNT
    CHECK_EQ(query[query.size() - 3], 12); // PTR
    CHECK_EQ(query[query.size() - 2], 0x80); // QU bit
}

// ---- WLED JSON ----

const char* kSampleStateInfo = R"({
  "state": {"on": true, "bri": 128, "transition": 7, "ps": -1, "pl": -1,
            "nl": {"on": false, "dur": 60, "mode": 1, "tbri": 0, "rem": -1},
            "udpn": {"send": false, "recv": true, "sgrp": 1, "rgrp": 3},
            "lor": 0, "mainseg": 1,
            "seg": [
              {"id": 0, "start": 0, "stop": 30, "col": [[1,2,3],[0,0,0],[0,0,0]], "fx": 2, "sx": 10, "ix": 20, "pal": 0, "sel": false},
              {"id": 1, "start": 30, "stop": 60, "col": [[255,160,0,0],[0,0,0,0],[0,0,0,0]], "fx": 9, "sx": 200, "ix": 77, "pal": 5, "sel": true}
            ]},
  "info": {"ver": "0.15.0", "vid": 2412100, "leds": {"count": 60, "pwr": 420, "fps": 42, "maxpwr": 850, "maxseg": 32},
           "name": "Desk Strip", "udpport": 21324, "live": false, "lm": "", "lip": "", "ws": 1,
           "fxcount": 187, "palcount": 71, "wifi": {"bssid": "x", "rssi": -61, "signal": 78, "channel": 6},
           "fs": {"u": 12, "t": 983, "pmt": 0}, "ndc": 4, "arch": "esp32", "core": "v3.3.6",
           "freeheap": 154321, "uptime": 93784, "brand": "WLED", "product": "FOSS", "mac": "aabbccddeeff", "ip": "10.0.0.42"}
})";

void test_parse_state_info() {
    model::WledState state;
    model::WledInfo info;
    std::string error;
    CHECK(platform::wled::parse_state_info(kSampleStateInfo, state, info, error));
    CHECK(state.on);
    CHECK_EQ(state.bri, 128);
    CHECK_EQ(state.main_segment, 1);
    CHECK_EQ(state.fx, 9);       // from the main segment, not segment 0
    CHECK_EQ(state.sx, 200);
    CHECK_EQ(state.ix, 77);
    CHECK_EQ(state.pal, 5);
    CHECK(state.color == (model::WledColor{255, 160, 0}));
    CHECK(state.any_segment_selected);
    CHECK(!state.sync_send);
    CHECK(state.sync_recv);
    CHECK_EQ(state.receive_groups, 3);

    CHECK_EQ(info.name, std::string("Desk Strip"));
    CHECK_EQ(info.version, std::string("0.15.0"));
    CHECK_EQ(info.led_count, 60);
    CHECK_EQ(info.power_ma, 420);
    CHECK_EQ(info.max_power_ma, 850);
    CHECK_EQ(info.fps, 42);
    CHECK_EQ(info.wifi_signal, 78);
    CHECK_EQ(info.wifi_rssi, -61);
    CHECK_EQ(info.uptime_s, 93784);
    CHECK_EQ(info.free_heap, 154321);
    CHECK_EQ(info.effect_count, 187);
    CHECK_EQ(info.node_count, 4);
}

void test_parse_newer_firmware_sync() {
    // WLED 0.15+ derives "recv" from rgrp; a missing recv must follow the group mask.
    const char* body = R"({"state":{"on":false,"bri":5,"udpn":{"send":true,"sgrp":1,"rgrp":0},"seg":[{"id":0,"fx":0,"col":[[0,0,255]]}]},
                           "info":{"ver":"0.16.0","leds":{"count":1}}})";
    model::WledState state;
    model::WledInfo info;
    std::string error;
    CHECK(platform::wled::parse_state_info(body, state, info, error));
    CHECK(state.sync_send);
    CHECK(!state.sync_recv);
    CHECK(!state.any_segment_selected);
}

void test_parse_rejects_non_wled() {
    model::WledState state;
    model::WledInfo info;
    std::string error;
    CHECK(!platform::wled::parse_state_info("<html>router</html>", state, info, error));
    CHECK(!platform::wled::parse_state_info(R"({"state":{},"info":{"name":"x"}})", state, info, error));
}

void test_parse_effects_and_nodes() {
    std::vector<std::string> effects;
    std::string error;
    CHECK(platform::wled::parse_effects(R"(["Solid","Blink","RSVD","Rainbow"])", effects, error));
    CHECK_EQ(effects.size(), 4u);
    CHECK(platform::wled::is_placeholder_effect(effects[2]));
    CHECK(!platform::wled::is_placeholder_effect(effects[3]));

    std::vector<model::WledPeer> peers;
    CHECK(platform::wled::parse_nodes(
        R"({"nodes":[{"name":"Porch","type":32,"ip":"10.0.0.7","age":0,"vid":1},{"name":"bad","ip":""}]})",
        peers, error));
    CHECK_EQ(peers.size(), 1u);
    if (!peers.empty()) {
        CHECK_EQ(peers[0].ip, std::string("10.0.0.7"));
        CHECK_EQ(peers[0].name, std::string("Porch"));
    }
}

void test_patches() {
    model::WledState state;
    state.any_segment_selected = true;
    CHECK_EQ(platform::wled::patch_power(true), std::string(R"({"on":true})"));
    CHECK_EQ(platform::wled::patch_brightness(300), std::string(R"({"bri":255})"));
    CHECK_EQ(platform::wled::patch_color(state, {10, 20, 30}), std::string(R"({"seg":{"col":[[10,20,30]]}})"));
    CHECK_EQ(platform::wled::patch_effect(state, 9), std::string(R"({"seg":{"fx":9}})"));
    state.any_segment_selected = false;
    state.main_segment = 2;
    CHECK_EQ(platform::wled::patch_speed(state, 99), std::string(R"({"seg":{"id":2,"sx":99}})"));
    CHECK_EQ(platform::wled::patch_sync(true, false, 0, 5),
             std::string(R"({"udpn":{"recv":false,"rgrp":0,"send":true,"sgrp":1}})"));
    CHECK_EQ(platform::wled::patch_sync(true, true, 6, 3),
             std::string(R"({"udpn":{"recv":true,"rgrp":3,"send":true,"sgrp":6}})"));
    CHECK_EQ(platform::wled::patch_sync(false, true, 2, 0),
             std::string(R"({"udpn":{"recv":true,"rgrp":1,"send":false}})"));
}

void test_node_broadcast() {
    uint8_t packet[44] = {};
    packet[0] = 255;
    packet[1] = 1;
    packet[2] = 192; packet[3] = 168; packet[4] = 1; packet[5] = 77;
    std::memcpy(packet + 6, "Kitchen  ", 9);
    packet[38] = 0x80 | 32;
    packet[39] = 77;
    model::WledPeer peer;
    CHECK(platform::wled::parse_node_broadcast(packet, sizeof(packet), peer));
    CHECK_EQ(peer.ip, std::string("192.168.1.77"));
    CHECK_EQ(peer.name, std::string("Kitchen"));
    packet[1] = 0;
    CHECK(!platform::wled::parse_node_broadcast(packet, sizeof(packet), peer));
    CHECK(!platform::wled::parse_node_broadcast(packet, 10, peer));
}

// ---- colour ----

void test_color_roundtrip() {
    const model::WledColor samples[] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 160, 0},
                                        {12, 200, 180}, {255, 255, 255}, {0, 0, 0}};
    for (const auto& c : samples) {
        const auto back = model::hsv_to_rgb(model::rgb_to_hsv(c));
        CHECK(std::abs(back.r - c.r) <= 1 && std::abs(back.g - c.g) <= 1 && std::abs(back.b - c.b) <= 1);
    }
    const auto red = model::rgb_to_hsv({255, 0, 0});
    CHECK(std::fabs(red.h) < 0.01F && std::fabs(red.s - 1.0F) < 0.01F);
    const auto blue = model::rgb_to_hsv({0, 0, 255});
    CHECK(std::fabs(blue.h - 240.0F) < 0.01F);
    CHECK(std::fabs(model::wrap_hue(-30.0F) - 330.0F) < 0.01F);
    CHECK(std::fabs(model::wrap_hue(370.0F) - 10.0F) < 0.01F);
}

// ---- store ----

void test_device_store() {
    model::DeviceStore store;
    bool created = false;
    store.upsert("10.0.0.2", "zeta", model::DeviceSource::Mdns, &created);
    CHECK(created);
    store.upsert("10.0.0.1", "Alpha", model::DeviceSource::Mdns, &created);
    store.upsert("10.0.0.2", "other-name", model::DeviceSource::PeerList, &created);
    CHECK(!created);
    CHECK_EQ(store.size(), 2u);
    store.sort();
    CHECK_EQ(store.devices()[0].name, std::string("Alpha"));
    CHECK_EQ(store.index_of("10.0.0.2"), 1);
    CHECK_EQ(store.find("10.0.0.2")->name, std::string("zeta")); // first hint kept
    CHECK(store.remove("10.0.0.1"));
    CHECK_EQ(store.size(), 1u);
}

} // namespace

int main() {
    test_http_content_length();
    test_http_chunked();
    test_http_errors();
    test_mdns_parse();
    test_mdns_query();
    test_parse_state_info();
    test_parse_newer_firmware_sync();
    test_parse_rejects_non_wled();
    test_parse_effects_and_nodes();
    test_patches();
    test_node_broadcast();
    test_color_roundtrip();
    test_device_store();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
