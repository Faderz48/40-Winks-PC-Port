#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>

#include "librecomp/game.hpp"

namespace forty_winks::platform {

struct WindowSize {
    int width;
    int height;

    auto operator<=>(const WindowSize&) const = default;
};

constexpr WindowSize default_window_size{960, 720};

struct InputAssignments {
    bool player_one_controller;
    bool player_two_controller;
};

bool valid_window_size(WindowSize size);
bool initialize(WindowSize requested_size,
                const std::filesystem::path& settings_path,
                bool use_saved_size);
void shutdown();

WindowSize current_window_size();
bool request_window_size(WindowSize size);
InputAssignments input_assignments();

ultramodern::gfx_callbacks_t gfx_callbacks();
ultramodern::renderer::callbacks_t renderer_callbacks();
ultramodern::input::callbacks_t input_callbacks();
ultramodern::audio_callbacks_t audio_callbacks();
recomp::rsp::callbacks_t rsp_callbacks();
ultramodern::events::callbacks_t events_callbacks();
ultramodern::error_handling::callbacks_t error_callbacks();

bool first_vi_seen();
uint64_t discarded_display_lists();
uint64_t discarded_rsp_tasks();

} // namespace forty_winks::platform
