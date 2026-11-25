/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "base_viewmodel.h"

#include "color.h"
#include "http_client.h"
#include "wled_api.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace viewmodel {
namespace {

using model::AppPage;
using model::Device;

constexpr uint32_t kTickMs = 100;
constexpr uint32_t kSelectedPollMs = 1000;    // device open on Control/Info/Effects
constexpr uint32_t kListPollMs = 3000;        // every device while the list is visible
constexpr uint32_t kBackgroundPollMs = 10000; // other devices while a detail page is open
constexpr uint32_t kOfflinePollMs = 15000;
constexpr uint32_t kConfirmPollMs = 400;      // re-read state shortly after a command
constexpr uint32_t kRediscoverMs = 60000;
constexpr int kOfflineAfterFailures = 2;

constexpr int kBrightnessStep = 13;  // ~5 %
constexpr float kHueStep = 10.0F;
constexpr float kSaturationStep = 0.1F;
constexpr int kEffectParamStep = 16;
constexpr int kEffectPageStep = 5;

bool due(uint32_t now, uint32_t at) {
    return static_cast<int32_t>(now - at) >= 0;
}

int page_to_int(AppPage page) {
    return static_cast<int>(page);
}

bool is_detail_page(AppPage page) {
    return page == AppPage::Control || page == AppPage::Info || page == AppPage::Effects;
}

model::DeviceSource to_source(platform::wled::FoundVia via) {
    switch (via) {
        case platform::wled::FoundVia::Mdns:
            return model::DeviceSource::Mdns;
        case platform::wled::FoundVia::UdpBroadcast:
            return model::DeviceSource::UdpBroadcast;
        case platform::wled::FoundVia::PeerList:
            return model::DeviceSource::PeerList;
    }
    return model::DeviceSource::Mdns;
}

std::string lower(const std::string& value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

} // namespace

BaseViewModel::BaseViewModel()
    : title_subject_("ZeroLED"),
      dark_mode_subject_(model_.dark_mode()),
      current_page_subject_(page_to_int(model_.current_page())),
      quit_requested_subject_(false),
      revision_subject_(0) {}

BaseViewModel::~BaseViewModel() {
    stop_network();
}

// ---------------------------------------------------------------------------------------------
// subjects

lv_subject_t* BaseViewModel::title_subject() { return title_subject_.native(); }
lv_subject_t* BaseViewModel::dark_mode_subject() { return dark_mode_subject_.native(); }
lv_subject_t* BaseViewModel::current_page_subject() { return current_page_subject_.native(); }
lv_subject_t* BaseViewModel::quit_requested_subject() { return quit_requested_subject_.native(); }
lv_subject_t* BaseViewModel::revision_subject() { return revision_subject_.native(); }

// ---------------------------------------------------------------------------------------------
// lifecycle

void BaseViewModel::start_network() {
    if (network_started_) {
        return;
    }
    network_started_ = true;
    service_.start();
    tick_timer_ = lv_timer_create(tick_cb, kTickMs, this);
    rescan();
}

void BaseViewModel::stop_network() {
    if (tick_timer_) {
        lv_timer_delete(tick_timer_);
        tick_timer_ = nullptr;
    }
    if (network_started_) {
        service_.stop();
        network_started_ = false;
    }
}

void BaseViewModel::set_toast_sink(std::function<void(const std::string&, ToastKind)> sink) {
    toast_sink_ = std::move(sink);
}

void BaseViewModel::set_manual_devices_changed(
    std::function<void(const std::vector<std::string>&)> callback) {
    manual_devices_changed_ = std::move(callback);
}

void BaseViewModel::load_saved_devices(const std::vector<std::string>& ips) {
    for (const auto& ip : ips) {
        if (!platform::net::is_device_address(ip)) {
            continue;
        }
        store_.upsert(ip, "", model::DeviceSource::Saved);
        manual_ips_.insert(ip);
    }
    store_.sort();
    bump();
}

std::vector<std::string> BaseViewModel::manual_device_ips() const {
    return {manual_ips_.begin(), manual_ips_.end()};
}

void BaseViewModel::toast(const std::string& message, ToastKind kind) {
    if (toast_sink_) {
        toast_sink_(message, kind);
    }
}

// ---------------------------------------------------------------------------------------------
// theme / navigation

bool BaseViewModel::is_dark_mode() const {
    return model_.dark_mode();
}

void BaseViewModel::set_dark_mode(bool enabled) {
    model_.set_dark_mode(enabled);
    dark_mode_subject_.set(enabled);
}

void BaseViewModel::toggle_dark_mode() {
    set_dark_mode(!model_.dark_mode());
}

AppPage BaseViewModel::current_page() const {
    return model_.current_page();
}

void BaseViewModel::show_page(AppPage page) {
    const uint32_t now = lv_tick_get();
    if (auto* device = mutable_selected(); device && is_detail_page(page)) {
        ensure_effects(*device);
        if (!is_detail_page(model_.current_page())) {
            device->next_poll_ms = now; // fresh numbers when a detail page opens
        }
        if (page == AppPage::Effects) {
            rebuild_effect_order();
            const auto it = std::find(effect_order_.begin(), effect_order_.end(), device->state.fx);
            effect_cursor_ = it == effect_order_.end() ? 0 : static_cast<int>(it - effect_order_.begin());
        }
    }
    if (page == AppPage::AddDevice) {
        ip_draft_.clear();
    }
    model_.set_current_page(page);
    current_page_subject_.set(page_to_int(page));
    bump();
}

void BaseViewModel::back() {
    switch (model_.current_page()) {
        case AppPage::Control:
        case AppPage::AddDevice:
            show_page(AppPage::Devices);
            break;
        case AppPage::Effects:
        case AppPage::Info:
            show_page(AppPage::Control);
            break;
        case AppPage::Devices:
            break;
    }
}

void BaseViewModel::request_quit() {
    quit_requested_subject_.set(true);
}

void BaseViewModel::nav_action(int slot) {
    switch (model_.current_page()) {
        case AppPage::Devices:
            switch (slot) {
                case 0: request_quit(); break;
                case 1: toggle_power_at_cursor(); break;
                case 2: rescan(); break;
                case 3: show_page(AppPage::AddDevice); break;
                case 4: open_cursor_device(); break;
                default: break;
            }
            break;
        case AppPage::Control:
            switch (slot) {
                case 0: back(); break;
                case 1: toggle_power(); break;
                case 2: toggle_sync_both(); break;
                case 3: show_page(AppPage::Info); break;
                case 4: show_page(AppPage::Effects); break;
                default: break;
            }
            break;
        case AppPage::Effects:
            switch (slot) {
                case 0: back(); break;
                case 1: move_effect_cursor(-kEffectPageStep); break;
                case 3: move_effect_cursor(kEffectPageStep); break;
                case 4: apply_effect_at_cursor(); break;
                default: break;
            }
            break;
        case AppPage::Info:
            switch (slot) {
                case 0: back(); break;
                case 1: toggle_power(); break;
                case 4: show_page(AppPage::Control); break;
                default: break;
            }
            break;
        case AppPage::AddDevice:
            if (slot == 0) {
                back();
            }
            break;
    }
}

// ---------------------------------------------------------------------------------------------
// device list

const std::vector<Device>& BaseViewModel::devices() const {
    return store_.devices();
}

bool BaseViewModel::scanning() const {
    return scanning_;
}

int BaseViewModel::list_cursor() const {
    const int index = store_.index_of(cursor_ip_);
    return index < 0 ? 0 : index;
}

void BaseViewModel::move_list_cursor(int delta) {
    if (store_.empty()) {
        return;
    }
    const int count = static_cast<int>(store_.size());
    const int next = std::clamp(list_cursor() + delta, 0, count - 1);
    cursor_ip_ = store_.devices()[static_cast<std::size_t>(next)].ip;
    bump();
}

const Device* BaseViewModel::cursor_device() const {
    if (store_.empty()) {
        return nullptr;
    }
    return &store_.devices()[static_cast<std::size_t>(list_cursor())];
}

void BaseViewModel::open_cursor_device() {
    const auto* device = cursor_device();
    if (!device) {
        toast("No device selected", ToastKind::Warning);
        return;
    }
    selected_ip_ = device->ip;
    control_cursor_ = 0;
    show_page(AppPage::Control);
}

void BaseViewModel::toggle_power_at_cursor() {
    const auto* device = cursor_device();
    if (!device) {
        return;
    }
    auto* target = store_.find(device->ip);
    if (!target->has_state) {
        toast(target->display_name() + " is not responding", ToastKind::Warning);
        return;
    }
    ++target->generation;
    target->state.on = !target->state.on;
    send(*target, "on", platform::wled::patch_power(target->state.on));
}

void BaseViewModel::rescan() {
    if (!network_started_) {
        return;
    }
    last_discovery_ms_ = lv_tick_get();
    service_.discover();
    // Re-ask known controllers for their peer lists: catches devices mDNS misses.
    peers_requested_.clear();
    for (const auto& device : store_.devices()) {
        if (device.verified) {
            peers_requested_.insert(device.ip);
            service_.fetch_peers(device.ip);
        }
    }
    scanning_ = true;
    update_title();
    bump();
}

// ---------------------------------------------------------------------------------------------
// manual add

const std::string& BaseViewModel::ip_draft() const {
    return ip_draft_;
}

void BaseViewModel::ip_draft_append(char c) {
    if ((std::isdigit(static_cast<unsigned char>(c)) || c == '.') && ip_draft_.size() < 15) {
        ip_draft_.push_back(c);
        bump();
    }
}

void BaseViewModel::ip_draft_backspace() {
    if (!ip_draft_.empty()) {
        ip_draft_.pop_back();
        bump();
    }
}

void BaseViewModel::ip_draft_submit() {
    const std::string ip = ip_draft_;
    if (!platform::net::is_ipv4_address(ip)) {
        toast("Enter an IPv4 address, e.g. 192.168.1.50", ToastKind::Warning);
        return;
    }
    cursor_ip_ = ip;
    if (store_.find(ip)) {
        toast("Already in the list", ToastKind::Info);
        show_page(AppPage::Devices);
        return;
    }
    auto& device = store_.upsert(ip, "", model::DeviceSource::Manual);
    device.next_poll_ms = lv_tick_get();
    pending_manual_.insert(ip);
    store_.sort();
    toast("Checking " + ip + "...", ToastKind::Info);
    show_page(AppPage::Devices);
}

// ---------------------------------------------------------------------------------------------
// selected device

const Device* BaseViewModel::selected_device() const {
    return store_.find(selected_ip_);
}

Device* BaseViewModel::mutable_selected() {
    return store_.find(selected_ip_);
}

Device* BaseViewModel::begin_change() {
    auto* device = mutable_selected();
    if (!device) {
        return nullptr;
    }
    if (!device->has_state) {
        toast("Waiting for " + device->display_name(), ToastKind::Warning);
        return nullptr;
    }
    ++device->generation;
    return device;
}

void BaseViewModel::send(Device& device, const char* key, const std::string& patch) {
    service_.send(device.ip, key, patch, device.generation);
    bump();
}

int BaseViewModel::control_cursor() const {
    return control_cursor_;
}

void BaseViewModel::move_control_cursor(int delta) {
    control_cursor_ = std::clamp(control_cursor_ + delta, 0, static_cast<int>(ControlRow::Count) - 1);
    bump();
}

void BaseViewModel::adjust_control(int direction) {
    switch (static_cast<ControlRow>(control_cursor_)) {
        case ControlRow::Power:
            if (const auto* device = selected_device(); device && device->state.on != (direction > 0)) {
                toggle_power();
            }
            break;
        case ControlRow::Brightness:
            adjust_brightness(direction * kBrightnessStep);
            break;
        case ControlRow::Hue:
            adjust_hue(static_cast<float>(direction) * kHueStep);
            break;
        case ControlRow::Saturation:
            adjust_saturation(static_cast<float>(direction) * kSaturationStep);
            break;
        case ControlRow::Effect:
            step_effect(direction);
            break;
        case ControlRow::Speed:
            adjust_speed(direction * kEffectParamStep);
            break;
        case ControlRow::Intensity:
            adjust_intensity(direction * kEffectParamStep);
            break;
        case ControlRow::SyncSend:
            if (const auto* device = selected_device(); device && device->state.sync_send != (direction > 0)) {
                toggle_sync_send();
            }
            break;
        case ControlRow::SyncReceive:
            if (const auto* device = selected_device(); device && device->state.sync_recv != (direction > 0)) {
                toggle_sync_receive();
            }
            break;
        case ControlRow::Count:
            break;
    }
}

void BaseViewModel::activate_control() {
    switch (static_cast<ControlRow>(control_cursor_)) {
        case ControlRow::Power:
            toggle_power();
            break;
        case ControlRow::Effect:
            show_page(AppPage::Effects);
            break;
        case ControlRow::SyncSend:
            toggle_sync_send();
            break;
        case ControlRow::SyncReceive:
            toggle_sync_receive();
            break;
        default:
            break;
    }
}

void BaseViewModel::toggle_power() {
    if (auto* device = begin_change()) {
        device->state.on = !device->state.on;
        send(*device, "on", platform::wled::patch_power(device->state.on));
    }
}

void BaseViewModel::adjust_brightness(int delta) {
    if (auto* device = begin_change()) {
        device->state.bri = std::clamp(device->state.bri + delta, 1, 255);
        device->state.on = true; // WLED turns on when brightness is set without "on"
        send(*device, "bri", platform::wled::patch_brightness(device->state.bri));
    }
}

void BaseViewModel::sync_hsv_anchor(Device& device) {
    if (device.hsv_anchor_valid && device.hsv_anchor_color == device.state.color) {
        return;
    }
    const auto hsv = model::rgb_to_hsv(device.state.color);
    if (hsv.s > 0.02F || !device.hsv_anchor_valid) {
        device.hue_deg = hsv.h;
    }
    device.saturation = hsv.s;
    device.hsv_anchor_color = device.state.color;
    device.hsv_anchor_valid = true;
}

void BaseViewModel::apply_color(Device& device, float hue, float saturation) {
    float value = model::rgb_to_hsv(device.state.color).v;
    if (value < 0.05F) {
        value = 1.0F;
    }
    device.hue_deg = model::wrap_hue(hue);
    device.saturation = std::clamp(saturation, 0.0F, 1.0F);
    device.state.color = model::hsv_to_rgb({device.hue_deg, device.saturation, value});
    device.hsv_anchor_color = device.state.color;
    device.hsv_anchor_valid = true;
    send(device, "col", platform::wled::patch_color(device.state, device.state.color));
}

void BaseViewModel::adjust_hue(float delta_deg) {
    if (auto* device = begin_change()) {
        sync_hsv_anchor(*device);
        // Moving the hue of a white colour should show a colour, not stay white.
        const float saturation = device->saturation < 0.05F ? 1.0F : device->saturation;
        apply_color(*device, device->hue_deg + delta_deg, saturation);
    }
}

void BaseViewModel::adjust_saturation(float delta) {
    if (auto* device = begin_change()) {
        sync_hsv_anchor(*device);
        apply_color(*device, device->hue_deg, device->saturation + delta);
    }
}

float BaseViewModel::selected_hue() const {
    const auto* device = selected_device();
    if (!device) {
        return 0.0F;
    }
    if (device->hsv_anchor_valid && device->hsv_anchor_color == device->state.color) {
        return device->hue_deg;
    }
    return model::rgb_to_hsv(device->state.color).h;
}

float BaseViewModel::selected_saturation() const {
    const auto* device = selected_device();
    if (!device) {
        return 0.0F;
    }
    if (device->hsv_anchor_valid && device->hsv_anchor_color == device->state.color) {
        return device->saturation;
    }
    return model::rgb_to_hsv(device->state.color).s;
}

void BaseViewModel::step_effect(int direction) {
    auto* device = mutable_selected();
    if (!device) {
        return;
    }
    rebuild_effect_order();
    int next = device->state.fx;
    if (!effect_order_.empty()) {
        const auto it = std::find(effect_order_.begin(), effect_order_.end(), device->state.fx);
        int position = it == effect_order_.end() ? 0 : static_cast<int>(it - effect_order_.begin());
        position = (position + direction + static_cast<int>(effect_order_.size())) %
                   static_cast<int>(effect_order_.size());
        next = effect_order_[static_cast<std::size_t>(position)];
    }
    else if (device->info.effect_count > 0) {
        next = (device->state.fx + direction + device->info.effect_count) % device->info.effect_count;
    }
    set_effect(next);
}

void BaseViewModel::set_effect(int fx) {
    if (auto* device = begin_change()) {
        device->state.fx = fx;
        send(*device, "fx", platform::wled::patch_effect(device->state, fx));
    }
}

void BaseViewModel::adjust_speed(int delta) {
    if (auto* device = begin_change()) {
        device->state.sx = std::clamp(device->state.sx + delta, 0, 255);
        send(*device, "sx", platform::wled::patch_speed(device->state, device->state.sx));
    }
}

void BaseViewModel::adjust_intensity(int delta) {
    if (auto* device = begin_change()) {
        device->state.ix = std::clamp(device->state.ix + delta, 0, 255);
        send(*device, "ix", platform::wled::patch_intensity(device->state, device->state.ix));
    }
}

void BaseViewModel::toggle_sync_send() {
    if (auto* device = begin_change()) {
        device->state.sync_send = !device->state.sync_send;
        send(*device, "udpn",
             platform::wled::patch_sync(device->state.sync_send, device->state.sync_recv,
                                        device->saved_sync_groups, device->saved_receive_groups));
    }
}

void BaseViewModel::toggle_sync_receive() {
    if (auto* device = begin_change()) {
        device->state.sync_recv = !device->state.sync_recv;
        device->state.receive_groups = device->state.sync_recv ? device->saved_receive_groups : 0;
        send(*device, "udpn",
             platform::wled::patch_sync(device->state.sync_send, device->state.sync_recv,
                                        device->saved_sync_groups, device->saved_receive_groups));
    }
}

void BaseViewModel::toggle_sync_both() {
    if (auto* device = begin_change()) {
        const bool enable = !(device->state.sync_send || device->state.sync_recv);
        device->state.sync_send = enable;
        device->state.sync_recv = enable;
        device->state.receive_groups = enable ? device->saved_receive_groups : 0;
        send(*device, "udpn",
             platform::wled::patch_sync(enable, enable, device->saved_sync_groups,
                                        device->saved_receive_groups));
        toast(enable ? "Sync on" : "Sync off", ToastKind::Info);
    }
}

// ---------------------------------------------------------------------------------------------
// effects page

void BaseViewModel::ensure_effects(Device& device) {
    if (!device.effects_loaded && !device.effects_requested && network_started_) {
        device.effects_requested = true;
        service_.fetch_effects(device.ip);
    }
}

void BaseViewModel::rebuild_effect_order() {
    const auto* device = selected_device();
    if (!device) {
        effect_order_.clear();
        effect_order_ip_.clear();
        return;
    }
    if (effect_order_ip_ == device->ip && effect_order_size_ == device->effects.size()) {
        return;
    }
    effect_order_ip_ = device->ip;
    effect_order_size_ = device->effects.size();
    effect_order_.clear();
    for (std::size_t i = 0; i < device->effects.size(); ++i) {
        if (!platform::wled::is_placeholder_effect(device->effects[i])) {
            effect_order_.push_back(static_cast<int>(i));
        }
    }
    const auto& names = device->effects;
    std::sort(effect_order_.begin(), effect_order_.end(), [&](int a, int b) {
        if (a == 0 || b == 0) {
            return a == 0 && b != 0; // Solid first, as in the WLED UI
        }
        return lower(names[static_cast<std::size_t>(a)]) < lower(names[static_cast<std::size_t>(b)]);
    });
}

const std::vector<int>& BaseViewModel::effect_order() const {
    return effect_order_;
}

int BaseViewModel::effect_cursor() const {
    return effect_cursor_;
}

void BaseViewModel::move_effect_cursor(int delta) {
    if (effect_order_.empty()) {
        return;
    }
    effect_cursor_ = std::clamp(effect_cursor_ + delta, 0, static_cast<int>(effect_order_.size()) - 1);
    bump();
}

void BaseViewModel::apply_effect_at_cursor() {
    if (effect_cursor_ >= 0 && effect_cursor_ < static_cast<int>(effect_order_.size())) {
        set_effect(effect_order_[static_cast<std::size_t>(effect_cursor_)]);
    }
}

// ---------------------------------------------------------------------------------------------
// background work

void BaseViewModel::tick_cb(lv_timer_t* timer) {
    auto* self = static_cast<BaseViewModel*>(lv_timer_get_user_data(timer));
    if (self) {
        self->tick();
    }
}

void BaseViewModel::tick() {
    auto events = service_.drain();
    const int revision_before = revision_;
    for (auto& event : events) {
        handle_event(event);
    }

    const uint32_t now = lv_tick_get();
    schedule_polls(now);

    if (model_.current_page() == AppPage::Devices && !service_.discovering() &&
        due(now, last_discovery_ms_ + kRediscoverMs)) {
        last_discovery_ms_ = now;
        service_.discover(); // quiet periodic refresh; no title change
    }

    if (!events.empty() && revision_ == revision_before) {
        bump();
    }
}

void BaseViewModel::schedule_polls(uint32_t now) {
    const auto page = model_.current_page();
    for (auto& device : store_.devices()) {
        if (device.poll_in_flight || !due(now, device.next_poll_ms)) {
            continue;
        }
        service_.poll(device.ip, device.generation);
        device.poll_in_flight = true;

        uint32_t interval = kBackgroundPollMs;
        if (device.ip == selected_ip_ && is_detail_page(page)) {
            interval = kSelectedPollMs;
        }
        else if (page == AppPage::Devices || page == AppPage::AddDevice) {
            interval = kListPollMs;
        }
        if (device.consecutive_failures >= kOfflineAfterFailures) {
            interval = std::max(interval, kOfflinePollMs);
        }
        device.next_poll_ms = now + interval;
    }
}

void BaseViewModel::handle_event(platform::wled::ServiceEvent& event) {
    using platform::wled::EventKind;
    const uint32_t now = lv_tick_get();

    if (event.kind == EventKind::DiscoveryFinished) {
        scanning_ = false;
        if (!event.ok && !event.error.empty()) {
            toast("Discovery failed: " + event.error, ToastKind::Error);
        }
        update_title();
        return;
    }

    if (event.ip.empty()) {
        return;
    }

    if (event.kind == EventKind::DeviceFound) {
        bool created = false;
        auto& device = store_.upsert(event.ip, event.name, to_source(event.via), &created);
        if (!event.mac.empty() && device.mac.empty()) {
            device.mac = event.mac;
        }
        if (created) {
            device.next_poll_ms = now;
            store_.sort();
            update_title();
        }
        return;
    }

    auto* device = store_.find(event.ip);
    if (!device) {
        return;
    }

    switch (event.kind) {
        case EventKind::PollResult: {
            device->poll_in_flight = false;
            if (event.ok) {
                const bool first_contact = !device->verified;
                device->verified = true;
                device->online = true;
                device->consecutive_failures = 0;
                device->last_error.clear();
                device->info = event.info;
                if (!event.info.mac.empty()) {
                    device->mac = event.info.mac;
                }
                if (event.generation == device->generation) {
                    device->state = event.state;
                    device->has_state = true;
                    if (event.state.receive_groups > 0) {
                        device->saved_receive_groups = event.state.receive_groups;
                    }
                    if (event.state.sync_groups > 0) {
                        device->saved_sync_groups = event.state.sync_groups;
                    }
                }
                else {
                    // A local edit happened while this poll was out; re-read soon.
                    device->next_poll_ms = now + kConfirmPollMs;
                }
                if (!event.info.name.empty() && event.info.name != device->name) {
                    device->name = event.info.name;
                    store_.sort();
                    device = store_.find(event.ip); // sort moved it
                }
                if (pending_manual_.erase(event.ip) > 0) {
                    manual_ips_.insert(event.ip);
                    toast("Added " + device->display_name(), ToastKind::Success);
                    if (manual_devices_changed_) {
                        manual_devices_changed_(manual_device_ips());
                    }
                }
                if (first_contact && peers_requested_.insert(event.ip).second) {
                    service_.fetch_peers(event.ip);
                }
                if (event.ip == selected_ip_) {
                    ensure_effects(*device);
                }
            }
            else {
                ++device->consecutive_failures;
                device->last_error = event.error;
                if (device->consecutive_failures >= kOfflineAfterFailures) {
                    device->online = false;
                }
                if (pending_manual_.count(event.ip) && !device->verified) {
                    pending_manual_.erase(event.ip);
                    toast("No WLED at " + event.ip + " (" + event.error + ")", ToastKind::Error);
                    if (cursor_ip_ == event.ip) {
                        cursor_ip_.clear();
                    }
                    store_.remove(event.ip);
                    update_title();
                }
            }
            break;
        }
        case EventKind::EffectsResult:
            if (event.ok) {
                device->effects = std::move(event.effects);
                device->effects_loaded = true;
                if (event.ip == selected_ip_) {
                    rebuild_effect_order();
                    if (model_.current_page() == AppPage::Effects) {
                        const auto it = std::find(effect_order_.begin(), effect_order_.end(), device->state.fx);
                        effect_cursor_ = it == effect_order_.end() ? 0 : static_cast<int>(it - effect_order_.begin());
                    }
                }
            }
            else {
                device->effects_requested = false; // retry on next page open
                if (event.ip == selected_ip_) {
                    toast("Could not load effects: " + event.error, ToastKind::Warning);
                }
            }
            break;
        case EventKind::CommandResult:
            if (!event.ok) {
                toast(device->display_name() + ": " + event.error, ToastKind::Error);
                device->next_poll_ms = now; // resync the UI with the real state
            }
            else if (event.generation == device->generation) {
                device->next_poll_ms = now + kConfirmPollMs;
            }
            break;
        default:
            break;
    }
}

void BaseViewModel::bump() {
    update_title();
    revision_subject_.set(++revision_);
}

void BaseViewModel::update_title() {
    char title[48];
    switch (model_.current_page()) {
        case AppPage::Devices:
            if (scanning_) {
                std::snprintf(title, sizeof(title), "ZeroLED  scanning...");
            }
            else {
                std::snprintf(title, sizeof(title), "ZeroLED  %zu found", store_.size());
            }
            break;
        case AppPage::AddDevice:
            std::snprintf(title, sizeof(title), "Add WLED by IP");
            break;
        case AppPage::Effects:
        case AppPage::Control:
        case AppPage::Info: {
            const auto* device = selected_device();
            std::snprintf(title, sizeof(title), "%s", device ? device->display_name().c_str() : "ZeroLED");
            break;
        }
    }
    if (std::string(title_subject_.value()) != title) {
        title_subject_.set(title);
    }
}

} // namespace viewmodel
