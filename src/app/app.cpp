/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "app.h"

#include "application_config.h"
#include "asset_manager.h"
#include "help_popup.h"
#include "logger.h"
#include "linux_input.h"
#include "screenshot_service.h"
#include "screen_manager.h"
#include "base_viewmodel.h"
#include "input_keys.h"
#include "theme.h"
#include "toast.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#if USE_DESKTOP
#include "desktop_simulator_frame.h"
#endif

#if !USE_DESKTOP
#if APP_USE_DRM
#include "src/drivers/display/drm/lv_linux_drm.h"
#else
#include "src/drivers/display/fb/lv_linux_fbdev.h"
#endif
#endif

#ifndef APP_FRAMEBUFFER_DEVICE
#define APP_FRAMEBUFFER_DEVICE "/dev/fb0"
#endif

#ifndef APP_DRM_DEVICE
#define APP_DRM_DEVICE "/dev/dri/card0"
#endif

#ifndef APP_DRM_CONNECTOR_ID
#define APP_DRM_CONNECTOR_ID -1
#endif

#ifndef APP_CONFIG_FILE
#define APP_CONFIG_FILE "zeroled.conf"
#endif

namespace app {
namespace {

// Upper bound on one main-loop sleep, so SDL events (desktop) and stray work stay responsive.
constexpr uint32_t kMaxIdleMs = 33;

void quit_requested_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* running = static_cast<bool*>(lv_observer_get_user_data(observer));
    if (running && lv_subject_get_int(subject)) {
        *running = false;
    }
}

// Owns the settings file: dark mode plus the controllers the user added by IP.
struct ConfigPersistence {
    std::string config_path;
    ApplicationConfig config;

    void save() {
        std::string error;
        if (save_application_config(config_path, config, error)) {
            LOG_INFO("saved config: {}", config_path);
        } else {
            LOG_WARN("failed to save config {}: {}", config_path, error);
        }
    }
};

std::string writable_config_path() {
    if (const char* override_path = std::getenv("ZEROLED_CONFIG"); override_path && override_path[0]) {
        return override_path;
    }
#if USE_DESKTOP
    return APP_CONFIG_FILE;
#else
    if (const char* xdg_config_home = std::getenv("XDG_CONFIG_HOME")) {
        const std::filesystem::path root(xdg_config_home);
        if (!root.empty() && root.is_absolute()) {
            return (root / "zeroled" / "zeroled.conf").string();
        }
    }
    if (const char* home = std::getenv("HOME")) {
        const std::filesystem::path root(home);
        if (!root.empty() && root.is_absolute()) {
            return (root / ".config" / "zeroled" / "zeroled.conf").string();
        }
    }
    return APP_CONFIG_FILE;
#endif
}

void persist_dark_mode_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* persistence = static_cast<ConfigPersistence*>(lv_observer_get_user_data(observer));
    if (!persistence) {
        return;
    }
    const bool dark_mode = lv_subject_get_int(subject) != 0;
    if (dark_mode == persistence->config.dark_mode) {
        return;
    }
    persistence->config.dark_mode = dark_mode;
    persistence->save();
}

view::widgets::ToastTone to_tone(viewmodel::ToastKind kind) {
    switch (kind) {
        case viewmodel::ToastKind::Success:
            return view::widgets::ToastTone::Success;
        case viewmodel::ToastKind::Warning:
            return view::widgets::ToastTone::Warning;
        case viewmodel::ToastKind::Error:
            return view::widgets::ToastTone::Error;
        case viewmodel::ToastKind::Info:
            break;
    }
    return view::widgets::ToastTone::Default;
}

// Global keys (screenshot, help, ESC back/exit) and routing of everything else to the active
// screen. Keys a screen does not consume fall through to the nav bar (keys 4..8).
class ShortcutController {
public:
    ShortcutController(viewmodel::BaseViewModel& view_model,
                       ScreenManager& screens,
                       AssetManager& assets)
        : view_model_(view_model),
          screens_(screens),
          toast_(std::make_unique<view::widgets::Toast>(lv_layer_top(), view_model, assets)),
          help_popup_(std::make_unique<view::widgets::HelpPopup>(lv_layer_top(), view_model, assets)) {
        toast_->build();
        help_popup_->build();
        platform::set_global_key_listener(global_key_listener, this);
        platform::set_key_release_listener(key_release_listener, this);
    }

    ~ShortcutController() {
        stop_repeat();
        platform::clear_key_release_listener(key_release_listener, this);
        platform::clear_global_key_listener(global_key_listener, this);
    }

    void show_toast(const std::string& message, viewmodel::ToastKind kind) {
        const uint32_t duration = kind == viewmodel::ToastKind::Error ? 3600 : 2200;
        toast_->show(message, to_tone(kind), duration);
    }

private:
    static constexpr uint32_t kRepeatMs = 90;

    bool handle_key(uint32_t key, bool long_pressed) {
        if (key == platform::kKeyPrintScreen) {
            if (!long_pressed) {
                take_screenshot();
            }
            return true;
        }

        if (key == platform::kKeyHelp || (key == 'h' && !text_entry())) {
            if (!long_pressed) {
                toast_->hide();
                if (help_popup_->visible()) {
                    help_popup_->hide();
                }
                else {
                    help_popup_->show(view_model_.current_page());
                }
            }
            return true;
        }

        if (help_popup_->visible()) {
            if (key == LV_KEY_ESC && !long_pressed) {
                help_popup_->hide();
            }
            return true;
        }

        const bool home = view_model_.current_page() == model::AppPage::Devices;
        if (key == LV_KEY_ESC || (home && key == '4')) {
            handle_exit_or_back(key, long_pressed);
            return true;
        }

        auto* screen = screens_.active();
        if (!screen) {
            return false;
        }
        if (long_pressed) {
            // Held arrow/backspace: auto-repeat until release.
            if (view::is_repeatable(view::classify_key(key)) && repeat_key_ == 0 &&
                screen->handle_key(key)) {
                start_repeat(key);
                return true;
            }
            return consumed_on_press_;
        }
        consumed_on_press_ = screen->handle_key(key);
        return consumed_on_press_;
    }

    void handle_exit_or_back(uint32_t key, bool long_pressed) {
        const bool home = view_model_.current_page() == model::AppPage::Devices;
        if (long_pressed) {
            if (exit_key_pressed_ == key && exit_armed_) {
                toast_->hide();
                view_model_.request_quit();
            }
            return;
        }
        exit_key_pressed_ = key;
        exit_armed_ = home;
        if (exit_armed_) {
            toast_->show_persistent_highlighted("Hold ",
                                                key == '4' ? "4" : "ESC",
                                                " to exit",
                                                view::widgets::ToastTone::Warning);
        }
    }

    void handle_release(uint32_t key) {
        if (key == repeat_key_) {
            stop_repeat();
        }
        if (key != exit_key_pressed_) {
            return;
        }

        if (exit_armed_) {
            toast_->hide();
        } else if (key == LV_KEY_ESC) {
            view_model_.back();
        }
        exit_key_pressed_ = 0;
        exit_armed_ = false;
    }

    bool text_entry() const {
        return view_model_.current_page() == model::AppPage::AddDevice;
    }

    void take_screenshot() {
        const auto result = platform::screenshot::capture_active_screen();
        if (result.success) {
            LOG_INFO("screenshot saved: {}", result.path);
            toast_->show_highlighted("Saved to ",
                                     result.path,
                                     "",
                                     view::widgets::ToastTone::Success,
                                     5200,
                                     true);
        }
        else {
            LOG_WARN("screenshot failed: {}", result.error);
            toast_->show("Screenshot failed: " + result.error,
                         view::widgets::ToastTone::Error,
                         3200);
        }
    }

    void start_repeat(uint32_t key) {
        stop_repeat();
        repeat_key_ = key;
        repeat_timer_ = lv_timer_create(repeat_timer_cb, kRepeatMs, this);
    }

    void stop_repeat() {
        if (repeat_timer_) {
            lv_timer_delete(repeat_timer_);
            repeat_timer_ = nullptr;
        }
        repeat_key_ = 0;
    }

    static void repeat_timer_cb(lv_timer_t* timer) {
        auto* controller = static_cast<ShortcutController*>(lv_timer_get_user_data(timer));
        if (!controller) {
            return;
        }
        auto* screen = controller->screens_.active();
        if (!screen || !screen->handle_key(controller->repeat_key_)) {
            controller->stop_repeat();
        }
    }

    static bool global_key_listener(uint32_t key,
                                    const char*,
                                    bool long_pressed,
                                    void* user_data) {
        auto* controller = static_cast<ShortcutController*>(user_data);
        return controller && controller->handle_key(key, long_pressed);
    }

    static void key_release_listener(uint32_t key, const char*, void* user_data) {
        auto* controller = static_cast<ShortcutController*>(user_data);
        if (controller) {
            controller->handle_release(key);
        }
    }

    viewmodel::BaseViewModel& view_model_;
    ScreenManager& screens_;
    std::unique_ptr<view::widgets::Toast> toast_;
    std::unique_ptr<view::widgets::HelpPopup> help_popup_;
    uint32_t exit_key_pressed_{0};
    bool exit_armed_{false};
    bool consumed_on_press_{false};
    uint32_t repeat_key_{0};
    lv_timer_t* repeat_timer_{nullptr};
};

#if USE_DESKTOP
// Desktop-only automation for screenshots and UI checks. ZEROLED_SCRIPT holds comma
// separated steps: "wait:<ms>", "shot", "expect:<device name>" (abort unless the highlighted
// device has that name), "quit", key names (up, down, left, right, enter, esc, space, bksp,
// help) or single characters.
class ScriptRunner {
public:
    ScriptRunner(viewmodel::BaseViewModel& view_model, const char* script) : view_model_(view_model) {
        std::string current;
        for (const char* c = script; ; ++c) {
            if (*c == ',' || *c == '\0') {
                if (!current.empty()) {
                    steps_.push_back(current);
                }
                current.clear();
                if (*c == '\0') {
                    break;
                }
            }
            else {
                current.push_back(*c);
            }
        }
        timer_ = lv_timer_create(timer_cb, 50, this);
    }

    ~ScriptRunner() {
        if (timer_) {
            lv_timer_delete(timer_);
        }
    }

private:
    static uint32_t key_for(const std::string& step) {
        if (step == "up") return LV_KEY_UP;
        if (step == "down") return LV_KEY_DOWN;
        if (step == "left") return LV_KEY_LEFT;
        if (step == "right") return LV_KEY_RIGHT;
        if (step == "enter") return LV_KEY_ENTER;
        if (step == "esc") return LV_KEY_ESC;
        if (step == "space") return ' ';
        if (step == "bksp") return LV_KEY_BACKSPACE;
        if (step == "help") return platform::kKeyHelp;
        if (step.size() == 1) return static_cast<uint32_t>(static_cast<unsigned char>(step[0]));
        return 0;
    }

    static void timer_cb(lv_timer_t* timer) {
        static_cast<ScriptRunner*>(lv_timer_get_user_data(timer))->step();
    }

    void step() {
        if (lv_tick_get() < resume_at_) {
            return;
        }
        if (index_ >= steps_.size()) {
            lv_timer_pause(timer_);
            return;
        }
        const std::string step = steps_[index_++];
        LOG_INFO("script: {}", step);
        if (step.rfind("wait:", 0) == 0) {
            resume_at_ = lv_tick_get() + static_cast<uint32_t>(std::atoi(step.c_str() + 5));
        }
        else if (step == "shot") {
            const auto result = platform::screenshot::capture_active_screen();
            LOG_INFO("script screenshot: {}", result.success ? result.path : result.error);
            resume_at_ = lv_tick_get() + 150;
        }
        else if (step.rfind("expect:", 0) == 0) {
            const auto* device = view_model_.cursor_device();
            const std::string want = step.substr(7);
            if (!device || device->display_name() != want) {
                LOG_ERROR("script: expected '{}' highlighted, found '{}'; stopping", want,
                          device ? device->display_name() : std::string("<none>"));
                index_ = steps_.size();
                view_model_.request_quit();
            }
        }
        else if (step == "quit") {
            view_model_.request_quit();
        }
        else if (const auto key = key_for(step)) {
            platform::simulate_key(key, true);
            platform::simulate_key(key, false);
            resume_at_ = lv_tick_get() + 120;
        }
        else {
            LOG_WARN("script: unknown step '{}'", step);
        }
    }

    viewmodel::BaseViewModel& view_model_;
    std::vector<std::string> steps_;
    std::size_t index_{0};
    uint32_t resume_at_{0};
    lv_timer_t* timer_{nullptr};
};
#endif

#if !USE_DESKTOP
lv_display_t* init_device_display() {
#if APP_USE_DRM
    auto* display = lv_linux_drm_create();
    if (!display) {
        return nullptr;
    }

    if (lv_linux_drm_set_file(display, APP_DRM_DEVICE, APP_DRM_CONNECTOR_ID) != LV_RESULT_OK) {
        lv_display_delete(display);
        return nullptr;
    }

    platform::init_key_input(display);
    return display;
#else
    auto* display = lv_linux_fbdev_create();
    if (!display) {
        return nullptr;
    }

    if (lv_linux_fbdev_set_file(display, APP_FRAMEBUFFER_DEVICE) != LV_RESULT_OK) {
        lv_display_delete(display);
        return nullptr;
    }

    platform::init_key_input(display);
    return display;
#endif
}
#endif

} // namespace

int Application::run() {
    logger::Logger::init();
    logger::Logger::set_tag("zeroled");

    lv_init();

    AssetManager assets;
    for (const auto& root : assets.roots()) {
        LOG_INFO("asset root: {}", root.string());
    }

    viewmodel::BaseViewModel view_model;
    const std::string user_config_path = writable_config_path();
    std::string loaded_config_path = APP_CONFIG_FILE;
    if (const char* override_path = std::getenv("ZEROLED_CONFIG"); override_path && override_path[0]) {
        loaded_config_path = override_path;
    }
    else if (user_config_path != APP_CONFIG_FILE) {
        std::error_code filesystem_error;
        if (std::filesystem::is_regular_file(user_config_path, filesystem_error)) {
            loaded_config_path = user_config_path;
        }
    }

    ConfigPersistence persistence{user_config_path, {}};
    std::string config_error;
    if (load_application_config(loaded_config_path, persistence.config, config_error)) {
        LOG_INFO("loaded config: {} (dark_mode={}, {} saved devices)",
                 loaded_config_path,
                 persistence.config.dark_mode ? "yes" : "no",
                 persistence.config.manual_devices.size());
    } else {
        LOG_WARN("failed to load config {}: {}; using defaults", loaded_config_path, config_error);
    }
    view_model.set_dark_mode(persistence.config.dark_mode);
    view_model.load_saved_devices(persistence.config.manual_devices);
    view_model.set_manual_devices_changed([&persistence](const std::vector<std::string>& ips) {
        persistence.config.manual_devices = ips;
        persistence.save();
    });

#if USE_DESKTOP
    DesktopSimulatorFrame simulator_frame(assets);
    auto* display = simulator_frame.display();
#else
    auto* display = init_device_display();
#endif
    if (!display) {
        LOG_ERROR("failed to initialize display");
        return 1;
    }

    auto* standard_font = assets.load_standard_font(14);
    view::apply_lvgl_theme(display,
                           view_model.is_dark_mode(),
                           standard_font ? standard_font : LV_FONT_DEFAULT);

#if USE_DESKTOP
    simulator_frame.bind_dark_mode(view_model.dark_mode_subject());
#endif

    ScreenManager screen_manager(view_model, assets);
    screen_manager.start();
    ShortcutController shortcuts(view_model, screen_manager, assets);
    view_model.set_toast_sink([&shortcuts](const std::string& message, viewmodel::ToastKind kind) {
        shortcuts.show_toast(message, kind);
    });

    auto* dark_mode_observer = lv_subject_add_observer(view_model.dark_mode_subject(),
                                                        persist_dark_mode_observer,
                                                        &persistence);

    bool running = true;
    auto* quit_observer = lv_subject_add_observer(view_model.quit_requested_subject(),
                                                  quit_requested_observer,
                                                  &running);

    view_model.start_network();
#if USE_DESKTOP
    std::unique_ptr<ScriptRunner> script;
    if (const char* steps = std::getenv("ZEROLED_SCRIPT"); steps && steps[0]) {
        script = std::make_unique<ScriptRunner>(view_model, steps);
    }
#endif

    LOG_INFO("LVGL app started at {}x{}", lv_display_get_horizontal_resolution(display),
             lv_display_get_vertical_resolution(display));
    while (running
#if USE_DESKTOP
           && simulator_frame.process_events()
#endif
    ) {
        // Sleep until the next LVGL timer is due. lv_delay_ms() busy-waits on the device,
        // where no delay callback is registered, and would pin a CPU core.
        uint32_t idle_ms = lv_timer_handler();
        if (idle_ms == LV_NO_TIMER_READY || idle_ms > kMaxIdleMs) {
            idle_ms = kMaxIdleMs;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(idle_ms > 0 ? idle_ms : 1));
    }

    view_model.stop_network();
    view_model.set_toast_sink(nullptr);
    view_model.set_manual_devices_changed(nullptr);

    if (quit_observer) {
        lv_observer_remove(quit_observer);
    }
    if (dark_mode_observer) {
        lv_observer_remove(dark_mode_observer);
    }

    return 0;
}

} // namespace app
