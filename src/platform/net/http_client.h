/*
 * SPDX-License-Identifier: MIT
 *
 * Minimal blocking HTTP/1.1 client for talking to WLED on the local network. Plain HTTP only
 * (WLED does not serve TLS). Call it from worker threads, never from the LVGL thread.
 */

#pragma once

#include <cstdint>
#include <string>

namespace platform::net {

struct HttpResponse {
    int status{0};
    std::string body;
};

// Performs one request with "Connection: close". timeout_ms bounds connect + send + receive.
bool http_request(const std::string& host,
                  uint16_t port,
                  const char* method,
                  const std::string& path,
                  const std::string& body,
                  int timeout_ms,
                  HttpResponse& response,
                  std::string& error);

// Device addresses are "a.b.c.d" or "a.b.c.d:port" (port defaults to 80).
bool split_address(const std::string& address, std::string& host, uint16_t& port);
bool is_device_address(const std::string& address);

bool http_get(const std::string& address, const std::string& path, int timeout_ms,
              HttpResponse& response, std::string& error);
bool http_post_json(const std::string& address, const std::string& path, const std::string& json,
                    int timeout_ms, HttpResponse& response, std::string& error);

// Exposed for tests: split a raw response into status and decoded body.
bool parse_http_response(const std::string& raw, HttpResponse& response, std::string& error);
bool decode_chunked(const std::string& input, std::string& output);

bool is_ipv4_address(const std::string& text);

} // namespace platform::net
