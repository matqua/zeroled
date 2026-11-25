/*
 * SPDX-License-Identifier: MIT
 */

#include "wled_service.h"

#include "mdns.h"
#include "wled_api.h"

#include <algorithm>
#include <cerrno>
#include <cstring>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace platform::wled {
namespace {

constexpr std::size_t kWorkerCount = 3;
constexpr uint16_t kNodeBroadcastPort = 65506;
constexpr const char* kWledService = "_wled._tcp.local";

} // namespace

WledService::WledService(ServiceTimeouts timeouts) : timeouts_(timeouts) {}

WledService::~WledService() {
    stop();
}

void WledService::start(bool listen_for_broadcasts) {
    if (started_) {
        return;
    }
    started_ = true;
    stopping_ = false;
    for (std::size_t i = 0; i < kWorkerCount; ++i) {
        workers_.emplace_back([this] { worker_loop(); });
    }
    discovery_thread_ = std::thread([this] { discovery_loop(); });
    if (listen_for_broadcasts) {
        broadcast_thread_ = std::thread([this] { broadcast_loop(); });
    }
}

void WledService::stop() {
    if (!started_) {
        return;
    }
    stopping_ = true;
    task_cv_.notify_all();
    discovery_cv_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();
    if (discovery_thread_.joinable()) {
        discovery_thread_.join();
    }
    if (broadcast_thread_.joinable()) {
        broadcast_thread_.join();
    }
    started_ = false;
}

void WledService::discover() {
    {
        std::lock_guard<std::mutex> lock(discovery_mutex_);
        discovery_requested_ = true;
    }
    discovering_ = true;
    discovery_cv_.notify_one();
}

void WledService::poll(const std::string& ip, uint32_t generation) {
    enqueue(Task{TaskKind::Poll, ip, "poll", "", generation}, false);
}

void WledService::fetch_effects(const std::string& ip) {
    enqueue(Task{TaskKind::Effects, ip, "effects", "", 0}, false);
}

void WledService::fetch_peers(const std::string& ip) {
    enqueue(Task{TaskKind::Peers, ip, "peers", "", 0}, false);
}

void WledService::send(const std::string& ip, const std::string& coalesce_key, std::string patch,
                       uint32_t generation) {
    enqueue(Task{TaskKind::Command, ip, coalesce_key, std::move(patch), generation}, true);
}

std::vector<ServiceEvent> WledService::drain() {
    std::vector<ServiceEvent> out;
    std::lock_guard<std::mutex> lock(event_mutex_);
    out.swap(events_);
    return out;
}

void WledService::push_event(ServiceEvent event) {
    std::lock_guard<std::mutex> lock(event_mutex_);
    events_.push_back(std::move(event));
}

void WledService::enqueue(Task task, bool front) {
    {
        std::lock_guard<std::mutex> lock(task_mutex_);
        // Coalesce: an unsent task with the same ip and key is replaced in place.
        for (auto& queued : tasks_) {
            if (queued.kind == task.kind && queued.ip == task.ip && queued.key == task.key) {
                queued = std::move(task);
                task_cv_.notify_one();
                return;
            }
        }
        if (front) {
            // Commands go ahead of polls but stay in order relative to other commands.
            auto it = std::find_if(tasks_.begin(), tasks_.end(),
                                   [](const Task& t) { return t.kind != TaskKind::Command; });
            tasks_.insert(it, std::move(task));
        }
        else {
            tasks_.push_back(std::move(task));
        }
    }
    task_cv_.notify_one();
}

void WledService::worker_loop() {
    while (true) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(task_mutex_);
            std::deque<Task>::iterator it;
            task_cv_.wait(lock, [&] {
                if (stopping_) {
                    return true;
                }
                it = std::find_if(tasks_.begin(), tasks_.end(), [&](const Task& t) {
                    return busy_ips_.count(t.ip) == 0;
                });
                return it != tasks_.end();
            });
            if (stopping_) {
                return;
            }
            task = std::move(*it);
            tasks_.erase(it);
            busy_ips_.insert(task.ip);
        }

        run_task(task);

        {
            std::lock_guard<std::mutex> lock(task_mutex_);
            busy_ips_.erase(task.ip);
        }
        task_cv_.notify_all();
    }
}

void WledService::run_task(const Task& task) {
    ServiceEvent event;
    event.ip = task.ip;
    event.generation = task.generation;

    switch (task.kind) {
        case TaskKind::Command:
            event.kind = EventKind::CommandResult;
            event.ok = post_state(task.ip, task.patch, timeouts_.command_ms, event.error);
            break;
        case TaskKind::Poll:
            event.kind = EventKind::PollResult;
            event.ok = fetch_state_info(task.ip, timeouts_.poll_ms, event.state, event.info, event.error);
            break;
        case TaskKind::Effects:
            event.kind = EventKind::EffectsResult;
            event.ok = ::platform::wled::fetch_effects(task.ip, timeouts_.effects_ms, event.effects, event.error);
            break;
        case TaskKind::Peers: {
            std::vector<model::WledPeer> peers;
            std::string error;
            if (fetch_nodes(task.ip, timeouts_.poll_ms, peers, error)) {
                for (const auto& peer : peers) {
                    ServiceEvent found;
                    found.kind = EventKind::DeviceFound;
                    found.ip = peer.ip;
                    found.name = peer.name;
                    found.via = FoundVia::PeerList;
                    push_event(std::move(found));
                }
            }
            return; // no event for the request itself
        }
    }
    push_event(std::move(event));
}

void WledService::discovery_loop() {
    while (true) {
        {
            std::unique_lock<std::mutex> lock(discovery_mutex_);
            discovery_cv_.wait(lock, [&] { return stopping_ || discovery_requested_; });
            if (stopping_) {
                return;
            }
            discovery_requested_ = false;
        }

        discovering_ = true;
        std::string error;
        net::mdns_browse(
            kWledService, timeouts_.discovery_ms, stopping_,
            [this](const net::MdnsService& service) {
                ServiceEvent event;
                event.kind = EventKind::DeviceFound;
                event.ip = service.ip;
                event.name = service.instance;
                event.mac = service.mac;
                event.via = FoundVia::Mdns;
                push_event(std::move(event));
            },
            error);

        ServiceEvent done;
        done.kind = EventKind::DiscoveryFinished;
        done.ok = error.empty();
        done.error = error;
        push_event(std::move(done));
        discovering_ = false;
    }
}

void WledService::broadcast_loop() {
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return;
    }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#ifdef SO_REUSEPORT
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
#endif
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(kNodeBroadcastPort);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        ::close(fd); // port in use; discovery still works through mDNS and peer lists
        return;
    }

    uint8_t buffer[256];
    while (!stopping_) {
        pollfd pfd{fd, POLLIN, 0};
        if (::poll(&pfd, 1, 250) <= 0) {
            continue;
        }
        sockaddr_in from{};
        socklen_t from_len = sizeof(from);
        const auto n = ::recvfrom(fd, buffer, sizeof(buffer), 0,
                                  reinterpret_cast<sockaddr*>(&from), &from_len);
        model::WledPeer peer;
        if (n > 0 && parse_node_broadcast(buffer, static_cast<std::size_t>(n), peer)) {
            ServiceEvent event;
            event.kind = EventKind::DeviceFound;
            event.ip = peer.ip;
            event.name = peer.name;
            event.via = FoundVia::UdpBroadcast;
            push_event(std::move(event));
        }
    }
    ::close(fd);
}

} // namespace platform::wled
