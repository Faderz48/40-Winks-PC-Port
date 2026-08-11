#include "debug_menu.hpp"

#include <algorithm>
#include <array>
#include <cstdio>

#include "debug_support.hpp"
#include "imgui/imgui.h"
#include "platform.hpp"
#include "rt64_renderer.hpp"

namespace forty_winks::debug_menu {
namespace {

struct ResolutionPreset {
    platform::WindowSize size;
    const char* label;
};

constexpr std::array<ResolutionPreset, 8> resolution_presets{{
    {{640, 480}, "640 x 480 (4:3)"},
    {{960, 720}, "960 x 720 (4:3)"},
    {{1280, 720}, "1280 x 720"},
    {{1280, 960}, "1280 x 960 (4:3)"},
    {{1600, 900}, "1600 x 900"},
    {{1920, 1080}, "1920 x 1080"},
    {{2560, 1440}, "2560 x 1440"},
    {{3840, 2160}, "3840 x 2160"},
}};

int selected_level = 0;
int custom_width = platform::default_window_size.width;
int custom_height = platform::default_window_size.height;
int selected_preset = 1;
bool initialized = false;

void initialize_controls() {
    if (initialized) {
        return;
    }

    const platform::WindowSize current_size = platform::current_window_size();
    custom_width = current_size.width;
    custom_height = current_size.height;
    for (size_t index = 0; index < resolution_presets.size(); ++index) {
        if (resolution_presets[index].size == current_size) {
            selected_preset = static_cast<int>(index);
            break;
        }
    }

    const debug::LevelState state = debug::level_state();
    if (state.current_level >= 0 &&
        state.current_level < static_cast<int>(debug::level_names.size())) {
        selected_level = state.current_level;
    }
    initialized = true;
}

void draw_level_tab() {
    const debug::LevelState state = debug::level_state();
    if (state.current_level >= 0) {
        ImGui::Text("Current: %02d  %s", state.current_level,
            debug::level_name(state.current_level));
    } else {
        ImGui::TextUnformatted("Current: Not loaded");
    }
    ImGui::Text("Game mode: %d", state.scene_mode);
    ImGui::Text("Status: %s", debug::level_load_status_text(state.status));
    ImGui::Separator();

    const float footer_height = ImGui::GetFrameHeightWithSpacing() +
        ImGui::GetStyle().ItemSpacing.y;
    if (ImGui::BeginChild("LevelList", ImVec2(0, -footer_height), true)) {
        for (size_t index = 0; index < debug::level_names.size(); ++index) {
            char label[96];
            const bool current = static_cast<int>(index) == state.current_level;
            std::snprintf(label, sizeof(label), "%02zu  %s%s", index,
                debug::level_names[index], current ? "  [CURRENT]" : "");
            if (ImGui::Selectable(label, selected_level == static_cast<int>(index))) {
                selected_level = static_cast<int>(index);
            }
        }
    }
    ImGui::EndChild();

    const bool request_in_flight = state.requested_level >= 0 || state.loading_level >= 0;
    const bool already_current = selected_level == state.current_level;
    ImGui::BeginDisabled(request_in_flight || already_current);
    if (ImGui::Button("Load selected level", ImVec2(-1, 0))) {
        debug::request_level_load(selected_level);
    }
    ImGui::EndDisabled();
}

void draw_display_tab() {
    const platform::WindowSize current_size = platform::current_window_size();
    constexpr int native_render_height = 240;
    const int internal_scale = std::max(
        (current_size.height + native_render_height - 1) / native_render_height,
        1);
    ImGui::Text("Current window: %d x %d", current_size.width, current_size.height);
    ImGui::Text("Internal render scale: %dx (automatic)", internal_scale);
    ImGui::Separator();

    constexpr const char* aspect_modes[] = {
        "Hybrid widescreen",
        "Original 4:3",
        "Stretch to window",
    };
    int aspect_mode = static_cast<int>(renderer::display_aspect_mode());
    if (ImGui::Combo(
        "Aspect mode", &aspect_mode, aspect_modes, std::size(aspect_modes))) {
        renderer::set_display_aspect_mode(
            static_cast<renderer::DisplayAspectMode>(aspect_mode));
    }

    if (ImGui::BeginCombo("Preset", resolution_presets[selected_preset].label)) {
        for (size_t index = 0; index < resolution_presets.size(); ++index) {
            const bool selected = selected_preset == static_cast<int>(index);
            if (ImGui::Selectable(resolution_presets[index].label, selected)) {
                selected_preset = static_cast<int>(index);
                custom_width = resolution_presets[index].size.width;
                custom_height = resolution_presets[index].size.height;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::InputInt("Width", &custom_width, 16, 160);
    ImGui::InputInt("Height", &custom_height, 9, 90);

    const platform::WindowSize requested_size{custom_width, custom_height};
    const bool valid = platform::valid_window_size(requested_size);
    const bool unchanged = requested_size == current_size;
    ImGui::BeginDisabled(!valid || unchanged);
    if (ImGui::Button("Apply resolution", ImVec2(-1, 0))) {
        platform::request_window_size(requested_size);
    }
    ImGui::EndDisabled();

    if (!valid) {
        ImGui::TextUnformatted("Supported range: 320 x 240 to 7680 x 4320");
    }
}

void draw_input_tab() {
    const platform::InputAssignments assignments = platform::input_assignments();
    if (assignments.player_one_controller) {
        ImGui::TextUnformatted("Player 1: Controller 1");
        ImGui::TextUnformatted(assignments.player_two_controller
            ? "Player 2: Keyboard + Controller 2"
            : "Player 2: Keyboard");
    } else {
        ImGui::TextUnformatted("Player 1: Keyboard");
        ImGui::TextUnformatted("Player 2: Not connected");
    }
}

} // namespace

void draw() {
    initialize_controls();

    const ImVec2 display_size = ImGui::GetIO().DisplaySize;
    const float width = std::min(640.0f, std::max(296.0f, display_size.x - 24.0f));
    const float height = std::min(700.0f, std::max(216.0f, display_size.y - 24.0f));
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(
        ImVec2(display_size.x * 0.5f, display_size.y * 0.5f),
        ImGuiCond_FirstUseEver,
        ImVec2(0.5f, 0.5f));

    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("40 Winks Debug Menu", nullptr, flags)) {
        if (ImGui::BeginTabBar("DebugTabs")) {
            if (ImGui::BeginTabItem("Levels")) {
                draw_level_tab();
                ImGui::EndTabItem();
            }
        if (ImGui::BeginTabItem("Display")) {
            draw_display_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Input")) {
            draw_input_tab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

} // namespace forty_winks::debug_menu
