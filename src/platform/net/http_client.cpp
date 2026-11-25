/*
 * SPDX-License-Identifier: MIT
 */

#include "http_client.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstring>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace platform::net {
namespace {

using Clock = std::chrono::steady_clock;

int remaining_ms(Clock::time_point deadline) {
    const auto left =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
    return left > 0 ? static_cast<int>(left) : 0;
}

class Socket {
public:
    Socket() = default;
    ~Socket() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    bool open() {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        return fd_ >= 0;
    }
    int fd() const { return fd_; }

private:
    int fd_{-1};
};

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

// Header lookup in the raw header block (case-insensitive name match).
std::string header_value(const std::string& headers, const std::string& name) {
    const auto lowered = lower(headers);
    const auto needle = "\r\n" + lower(name) + ":";
    const auto pos = lowered.find(needle);
    if (pos == std::string::npos) {
        return {};
    }
    const auto start = pos + needle.size();
    const auto end = headers.find("\r\n", start);
    std::string value = headers.substr(start, end == std::string::npos ? std::string::npos : end - start);
    value.erase(0, value.find_first_not_of(" \t"));
    value.erase(value.find_last_not_of(" \t") + 1);
    return value;
}

// True once raw holds a complete response we can stop reading for.
bool response_complete(const std::string& raw) {
    const auto header_end = raw.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        return false;
    }
    const std::string headers = raw.substr(0, header_end + 2);
    const auto body_len = raw.size() - (header_end + 4);

    const auto length = header_value(headers, "Content-Length");
    if (!length.empty()) {
        char* end = nullptr;
        const auto expected = std::strtoul(length.c_str(), &end, 10);
        return body_len >= expected;
    }
    if (lower(header_value(headers, "Transfer-Encoding")).find("chunked") != std::string::npos) {
        return raw.size() >= 5 && raw.compare(raw.size() - 5, 5, "0\r\n\r\n") == 0;
    }
    return false; // read until the peer closes
}

bool connect_with_timeout(int fd, const sockaddr_in& address, Clock::time_point deadline,
                          std::string& error) {
    const int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int rc = ::connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    if (rc == 0) {
        return true;
    }
    if (errno != EINPROGRESS) {
        error = std::string("connect: ") + std::strerror(errno);
        return false;
    }

    pollfd pfd{fd, POLLOUT, 0};
    rc = ::poll(&pfd, 1, remaining_ms(deadline));
    if (rc <= 0) {
        error = rc == 0 ? "connect timed out" : std::string("poll: ") + std::strerror(errno);
        return false;
    }

    int so_error = 0;
    socklen_t len = sizeof(so_error);
    getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len);
    if (so_error != 0) {
        error = std::string("connect: ") + std::strerror(so_error);
        return false;
    }
    return true;
}

bool send_all(int fd, const std::string& data, Clock::time_point deadline, std::string& error) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        pollfd pfd{fd, POLLOUT, 0};
        const int rc = ::poll(&pfd, 1, remaining_ms(deadline));
        if (rc <= 0) {
            error = rc == 0 ? "send timed out" : std::string("poll: ") + std::strerror(errno);
            return false;
        }
#ifdef MSG_NOSIGNAL
        const auto n = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
#else
        const auto n = ::send(fd, data.data() + sent, data.size() - sent, 0);
#endif
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                continue;
            }
            error = std::string("send: ") + std::strerror(errno);
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

} // namespace

bool is_ipv4_address(const std::string& text) {
    in_addr addr{};
    return inet_pton(AF_INET, text.c_str(), &addr) == 1;
}

bool split_address(const std::string& address, std::string& host, uint16_t& port) {
    const auto colon = address.find(':');
    host = address.substr(0, colon);
    port = 80;
    if (colon != std::string::npos) {
        const std::string port_text = address.substr(colon + 1);
        if (port_text.empty() || port_text.size() > 5 ||
            port_text.find_first_not_of("0123456789") != std::string::npos) {
            return false;
        }
        const long value = std::strtol(port_text.c_str(), nullptr, 10);
        if (value <= 0 || value > 65535) {
            return false;
        }
        port = static_cast<uint16_t>(value);
    }
    return is_ipv4_address(host);
}

bool is_device_address(const std::string& address) {
    std::string host;
    uint16_t port = 0;
    return split_address(address, host, port);
}

bool http_get(const std::string& address, const std::string& path, int timeout_ms,
              HttpResponse& response, std::string& error) {
    std::string host;
    uint16_t port = 80;
    if (!split_address(address, host, port)) {
        error = "bad address: " + address;
        return false;
    }
    return http_request(host, port, "GET", path, "", timeout_ms, response, error);
}

bool http_post_json(const std::string& address, const std::string& path, const std::string& json,
                    int timeout_ms, HttpResponse& response, std::string& error) {
    std::string host;
    uint16_t port = 80;
    if (!split_address(address, host, port)) {
        error = "bad address: " + address;
        return false;
    }
    return http_request(host, port, "POST", path, json, timeout_ms, response, error);
}

bool decode_chunked(const std::string& input, std::string& output) {
    output.clear();
    std::size_t pos = 0;
    while (pos < input.size()) {
        const auto line_end = input.find("\r\n", pos);
        if (line_end == std::string::npos) {
            return false;
        }
        const std::string size_text = input.substr(pos, line_end - pos);
        char* end = nullptr;
        const unsigned long size = std::strtoul(size_text.c_str(), &end, 16);
        if (end == size_text.c_str()) {
            return false;
        }
        pos = line_end + 2;
        if (size == 0) {
            return true;
        }
        if (pos + size > input.size()) {
            return false;
        }
        output.append(input, pos, size);
        pos += size + 2; // skip chunk data and its CRLF
    }
    return false;
}

bool parse_http_response(const std::string& raw, HttpResponse& response, std::string& error) {
    const auto header_end = raw.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        error = raw.empty() ? "empty response" : "truncated response headers";
        return false;
    }

    // Status line: HTTP/1.1 200 OK
    const auto first_space = raw.find(' ');
    if (first_space == std::string::npos || first_space > header_end ||
        raw.compare(0, 5, "HTTP/") != 0) {
        error = "malformed status line";
        return false;
    }
    response.status = std::atoi(raw.c_str() + first_space + 1);

    const std::string headers = raw.substr(0, header_end + 2);
    const std::string body = raw.substr(header_end + 4);
    if (lower(header_value(headers, "Transfer-Encoding")).find("chunked") != std::string::npos) {
        if (!decode_chunked(body, response.body)) {
            error = "bad chunked encoding";
            return false;
        }
        return true;
    }

    const auto length = header_value(headers, "Content-Length");
    if (!length.empty()) {
        const auto expected = std::strtoul(length.c_str(), nullptr, 10);
        if (body.size() < expected) {
            error = "truncated body";
            return false;
        }
        response.body = body.substr(0, expected);
        return true;
    }

    response.body = body;
    return true;
}

bool http_request(const std::string& host,
                  uint16_t port,
                  const char* method,
                  const std::string& path,
                  const std::string& body,
                  int timeout_ms,
                  HttpResponse& response,
                  std::string& error) {
    response = {};
    error.clear();
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
        error = "not an IPv4 address: " + host;
        return false;
    }

    Socket socket;
    if (!socket.open()) {
        error = std::string("socket: ") + std::strerror(errno);
        return false;
    }
#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(socket.fd(), SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    int nodelay = 1;
    setsockopt(socket.fd(), IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

    if (!connect_with_timeout(socket.fd(), address, deadline, error)) {
        return false;
    }

    std::string request;
    request.reserve(256 + body.size());
    request += method;
    request += ' ';
    request += path;
    request += " HTTP/1.1\r\nHost: ";
    request += host;
    request += "\r\nUser-Agent: zeroled-cardputerzero\r\nAccept: application/json\r\n"
               "Connection: close\r\n";
    if (!body.empty() || std::strcmp(method, "POST") == 0) {
        request += "Content-Type: application/json\r\nContent-Length: ";
        request += std::to_string(body.size());
        request += "\r\n";
    }
    request += "\r\n";
    request += body;

    if (!send_all(socket.fd(), request, deadline, error)) {
        return false;
    }

    std::string raw;
    char buffer[4096];
    while (true) {
        const int wait = remaining_ms(deadline);
        if (wait <= 0) {
            error = "response timed out";
            return false;
        }
        pollfd pfd{socket.fd(), POLLIN, 0};
        const int rc = ::poll(&pfd, 1, wait);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            error = std::string("poll: ") + std::strerror(errno);
            return false;
        }
        if (rc == 0) {
            error = "response timed out";
            return false;
        }
        const auto n = ::recv(socket.fd(), buffer, sizeof(buffer), 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                continue;
            }
            error = std::string("recv: ") + std::strerror(errno);
            return false;
        }
        if (n == 0) {
            break; // peer closed
        }
        raw.append(buffer, static_cast<std::size_t>(n));
        if (raw.size() > 512 * 1024) {
            error = "response too large";
            return false;
        }
        if (response_complete(raw)) {
            break;
        }
    }

    return parse_http_response(raw, response, error);
}

} // namespace platform::net
