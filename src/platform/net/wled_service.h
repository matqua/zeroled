/*
 * SPDX-License-Identifier: MIT
 *
 * Background networking for WLED. All sockets live on worker threads; the LVGL thread only
 * enqueues requests and drains ServiceEvents from a timer, so the UI never blocks on the network.
 *
 * - Discovery thread: mDNS "_wled._tcp" browse on demand.
 * - Broadcast thread: passive listener for WLED UDP node broadcasts on port 65506.
 * - Worker pool: polls, effect list fetches, peer list fetches and state commands. Requests for
 *   the same IP are serialised (so commands apply in order); different devices run in parallel,
 *   which keeps one offline controller from stalling the others.
 */

#pragma once

#include "wled_types.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace platform::wled {

enum class EventKind : uint8_t {
    DeviceFound,      // ip, name, mac, source
    DiscoveryFinished,
    PollResult,       // ip, ok, error, state, info, generation
    EffectsResult,    // ip, ok, error, effects
    CommandResult,    // ip, ok, error, generation
};

enum class FoundVia : uint8_t {
    Mdns,
    UdpBroadcast,
    PeerList,
};

struct ServiceEvent {
    EventKind kind{EventKind::DeviceFound};
    std::string ip;
    std::string name;
    std::string mac;
    FoundVia via{FoundVia::Mdns};
    bool ok{false};
    std::string error;
    uint32_t generation{0};
    model::WledState state;
    model::WledInfo info;
    std::vector<std::string> effects;
};

struct ServiceTimeouts {
    int poll_ms{1500};
    int command_ms{2000};
    int effects_ms{4000};
    int discovery_ms{2500};
};

class WledService {
public:
    explicit WledService(ServiceTimeouts timeouts = {});
    ~WledService();

    WledService(const WledService&) = delete;
    WledService& operator=(const WledService&) = delete;

    void start(bool listen_for_broadcasts = true);
    void stop();

    // All of these are cheap and safe to call from the LVGL thread.
    void discover();
    void poll(const std::string& ip, uint32_t generation);
    void fetch_effects(const std::string& ip);
    void fetch_peers(const std::string& ip);
    // Commands with the same (ip, coalesce_key) that have not been sent yet are replaced, so
    // holding a key only sends the latest brightness instead of a backlog.
    void send(const std::string& ip, const std::string& coalesce_key, std::string patch,
              uint32_t generation);

    std::vector<ServiceEvent> drain();
    bool discovering() const { return discovering_.load(); }

private:
    enum class TaskKind : uint8_t { Command, Poll, Effects, Peers };
    struct Task {
        TaskKind kind;
        std::string ip;
        std::string key;
        std::string patch;
        uint32_t generation{0};
    };

    void push_event(ServiceEvent event);
    void enqueue(Task task, bool front);
    void worker_loop();
    void discovery_loop();
    void broadcast_loop();
    void run_task(const Task& task);

    ServiceTimeouts timeouts_;
    std::atomic<bool> stopping_{false};
    std::atomic<bool> discovering_{false};
    bool started_{false};

    std::mutex task_mutex_;
    std::condition_variable task_cv_;
    std::deque<Task> tasks_;
    std::set<std::string> busy_ips_;

    std::mutex discovery_mutex_;
    std::condition_variable discovery_cv_;
    bool discovery_requested_{false};

    std::mutex event_mutex_;
    std::vector<ServiceEvent> events_;

    std::vector<std::thread> workers_;
    std::thread discovery_thread_;
    std::thread broadcast_thread_;
};

} // namespace platform::wled
