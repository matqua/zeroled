/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_model.h"
#include "device_store.h"
#include "subjects.h"
#include "wled_service.h"

#include "lvgl.h"

#include <functional>
#include <set>
#include <string>
#include <vector>

namespace viewmodel {

enum class ControlRow : int {
    Power = 0,
    Brightness,
    Hue,
    Saturation,
    Effect,
    Speed,
    Intensity,
    SyncSend,
    SyncReceive,
    Count,
};

enum class ToastKind {
    Info,
    Success,
    Warning,
    Error,
};

class BaseViewModel {
public:
    BaseViewModel();
    ~BaseViewModel();

    BaseViewModel(const BaseViewModel&) = delete;
    BaseViewModel& operator=(const BaseViewModel&) = delete;

    // ---- subjects ----
    lv_subject_t* title_subject();
    lv_subject_t* dark_mode_subject();
    lv_subject_t* current_page_subject();
    lv_subject_t* quit_requested_subject();
    // Bumped whenever device data, cursors or selection change; screens re-render on it.
    lv_subject_t* revision_subject();

    // ---- lifecycle ----
    // Starts the network workers and the LVGL drain timer. Call after lv_init().
    void start_network();
    void stop_network();
    void set_toast_sink(std::function<void(const std::string&, ToastKind)> sink);
    void set_manual_devices_changed(std::function<void(const std::vector<std::string>&)> callback);
    void load_saved_devices(const std::vector<std::string>& ips);
    std::vector<std::string> manual_device_ips() const;

    // ---- theme / navigation ----
    bool is_dark_mode() const;
    void set_dark_mode(bool enabled);
    void toggle_dark_mode();

    model::AppPage current_page() const;
    void show_page(model::AppPage page);
    void back();
    void request_quit();
    // Nav bar slot 0..4 (keys 4..8) for the current page.
    void nav_action(int slot);

    // ---- device list ----
    const std::vector<model::Device>& devices() const;
    bool scanning() const;
    int list_cursor() const;
    void move_list_cursor(int delta);
    const model::Device* cursor_device() const;
    void open_cursor_device();
    void toggle_power_at_cursor();
    void rescan();

    // ---- manual add ----
    const std::string& ip_draft() const;
    void ip_draft_append(char c);
    void ip_draft_backspace();
    void ip_draft_submit();

    // ---- selected device (control / info / effects pages) ----
    const model::Device* selected_device() const;
    int control_cursor() const;
    void move_control_cursor(int delta);
    void adjust_control(int direction);
    void activate_control();

    void toggle_power();
    void adjust_brightness(int delta);
    void adjust_hue(float delta_deg);
    void adjust_saturation(float delta);
    void step_effect(int direction);
    void set_effect(int fx);
    void adjust_speed(int delta);
    void adjust_intensity(int delta);
    void toggle_sync_send();
    void toggle_sync_receive();
    void toggle_sync_both();
    float selected_hue() const;
    float selected_saturation() const;

    // ---- effects page ----
    // Effect ids to show, alphabetical with "Solid" (id 0) first; placeholders removed.
    const std::vector<int>& effect_order() const;
    int effect_cursor() const;
    void move_effect_cursor(int delta);
    void apply_effect_at_cursor();

    // Exposed for the shortcut controller.
    void toast(const std::string& message, ToastKind kind = ToastKind::Info);

private:
    static void tick_cb(lv_timer_t* timer);
    void tick();
    void handle_event(platform::wled::ServiceEvent& event);
    void schedule_polls(uint32_t now);
    void bump();
    void update_title();
    model::Device* mutable_selected();
    model::Device* begin_change(); // selected device prepared for a local edit (or nullptr)
    void send(model::Device& device, const char* key, const std::string& patch);
    void ensure_effects(model::Device& device);
    void rebuild_effect_order();
    void sync_hsv_anchor(model::Device& device);
    void apply_color(model::Device& device, float hue, float saturation);

    model::BaseModel model_;
    model::DeviceStore store_;
    platform::wled::WledService service_;
    lv_timer_t* tick_timer_{nullptr};
    bool network_started_{false};

    std::string cursor_ip_;
    std::string selected_ip_;
    int control_cursor_{0};
    int effect_cursor_{0};
    std::vector<int> effect_order_;
    std::string effect_order_ip_;
    std::size_t effect_order_size_{0};
    std::string ip_draft_;
    std::set<std::string> pending_manual_;
    std::set<std::string> manual_ips_;
    std::set<std::string> peers_requested_;
    uint32_t last_discovery_ms_{0};
    bool scanning_{false};
    int revision_{0};

    std::function<void(const std::string&, ToastKind)> toast_sink_;
    std::function<void(const std::vector<std::string>&)> manual_devices_changed_;

    reactive::StringSubject<48> title_subject_;
    reactive::BoolSubject dark_mode_subject_;
    reactive::IntSubject current_page_subject_;
    reactive::BoolSubject quit_requested_subject_;
    reactive::IntSubject revision_subject_;
};

} // namespace viewmodel
