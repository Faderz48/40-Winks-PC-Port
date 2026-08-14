#include "platform.hpp"

#include <SDL.h>
#if defined(_WIN32)
#include <SDL_syswm.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>

#include "librecomp/rsp.hpp"
#include "input_routing.hpp"
#include "rt64_renderer.hpp"

namespace forty_winks::platform {
namespace {

constexpr Sint16 stick_deadzone = 7000;
constexpr Sint16 trigger_threshold = 16000;

namespace n64_button {
constexpr uint16_t a = 0x8000;
constexpr uint16_t b = 0x4000;
constexpr uint16_t z = 0x2000;
constexpr uint16_t start = 0x1000;
constexpr uint16_t dpad_up = 0x0800;
constexpr uint16_t dpad_down = 0x0400;
constexpr uint16_t dpad_left = 0x0200;
constexpr uint16_t dpad_right = 0x0100;
constexpr uint16_t l = 0x0020;
constexpr uint16_t r = 0x0010;
constexpr uint16_t c_up = 0x0008;
constexpr uint16_t c_down = 0x0004;
constexpr uint16_t c_left = 0x0002;
constexpr uint16_t c_right = 0x0001;
} // namespace n64_button

struct ControllerState {
    SDL_GameController* controller = nullptr;
    SDL_JoystickID instance_id = -1;
    uint16_t buttons = 0;
    float x = 0.0f;
    float y = 0.0f;
};

struct InputState {
    std::mutex mutex;
    uint16_t keyboard_buttons = 0;
    float keyboard_x = 0.0f;
    float keyboard_y = 0.0f;
    std::array<ControllerState, input_routing::player_count> controllers{};
};

InputState input;
SDL_Window* window = nullptr;
std::atomic<uint64_t> rsp_task_count = 0;
std::atomic<bool> vi_seen = false;
std::atomic<int> current_window_width = default_window_size.width;
std::atomic<int> current_window_height = default_window_size.height;
std::atomic<int> pending_window_width = default_window_size.width;
std::atomic<int> pending_window_height = default_window_size.height;
std::atomic<bool> window_resize_pending = false;
std::filesystem::path window_settings_path;
std::mutex window_settings_mutex;

WindowSize load_saved_window_size(WindowSize fallback) {
    if (window_settings_path.empty()) {
        return fallback;
    }

    std::ifstream input_file{window_settings_path};
    WindowSize saved{};
    if (input_file >> saved.width >> saved.height && valid_window_size(saved)) {
        return saved;
    }
    return fallback;
}

bool persist_window_size(WindowSize size) {
    std::lock_guard lock{window_settings_mutex};
    if (window_settings_path.empty()) {
        return true;
    }

    std::filesystem::path temporary_path = window_settings_path;
    temporary_path += ".tmp";
    std::ofstream output{temporary_path, std::ios::trunc};
    if (!output) {
        return false;
    }
    output << size.width << ' ' << size.height << '\n';
    output.flush();
    output.close();
    if (!output) {
        std::error_code cleanup_error;
        std::filesystem::remove(temporary_path, cleanup_error);
        return false;
    }

    std::error_code rename_error;
    std::filesystem::rename(temporary_path, window_settings_path, rename_error);
    if (rename_error) {
        std::error_code cleanup_error;
        std::filesystem::remove(temporary_path, cleanup_error);
        return false;
    }
    return true;
}

float normalize_axis(Sint16 value) {
    if (std::abs(static_cast<int>(value)) <= stick_deadzone) {
        return 0.0f;
    }

    const float normalized = value < 0
        ? static_cast<float>(value) / 32768.0f
        : static_cast<float>(value) / 32767.0f;
    return std::clamp(normalized, -1.0f, 1.0f);
}

bool controller_is_open_locked(SDL_JoystickID instance_id) {
    return std::ranges::any_of(input.controllers,
        [instance_id](const ControllerState& state) {
            return state.controller != nullptr && state.instance_id == instance_id;
        });
}

bool open_controller_device_locked(int device_index) {
    if (!SDL_IsGameController(device_index)) {
        return false;
    }

    const SDL_JoystickID device_instance =
        SDL_JoystickGetDeviceInstanceID(device_index);
    if (device_instance >= 0 && controller_is_open_locked(device_instance)) {
        return false;
    }

    const auto free_slot = std::ranges::find_if(input.controllers,
        [](const ControllerState& state) { return state.controller == nullptr; });
    if (free_slot == input.controllers.end()) {
        return false;
    }

    SDL_GameController* controller = SDL_GameControllerOpen(device_index);
    if (controller == nullptr) {
        std::fprintf(stderr, "Could not open controller: %s\n", SDL_GetError());
        return false;
    }

    SDL_Joystick* joystick = SDL_GameControllerGetJoystick(controller);
    if (joystick == nullptr) {
        SDL_GameControllerClose(controller);
        return false;
    }

    const size_t slot = static_cast<size_t>(free_slot - input.controllers.begin());
    *free_slot = {
        .controller = controller,
        .instance_id = SDL_JoystickInstanceID(joystick),
    };
    const char* name = SDL_GameControllerName(controller);
    std::printf("Controller connected for Player %zu: %s\n",
        slot + 1, name != nullptr ? name : "Unknown controller");
    return true;
}

void open_available_controllers_locked() {
    for (int device_index = 0; device_index < SDL_NumJoysticks(); ++device_index) {
        open_controller_device_locked(device_index);
    }
}

void open_available_controllers() {
    std::lock_guard lock{input.mutex};
    open_available_controllers_locked();
}

void remove_controller(SDL_JoystickID instance_id) {
    std::lock_guard lock{input.mutex};
    bool removed = false;
    for (ControllerState& state : input.controllers) {
        if (state.controller != nullptr && state.instance_id == instance_id) {
            SDL_GameControllerClose(state.controller);
            state = {};
            removed = true;
            break;
        }
    }

    if (!removed) {
        return;
    }

    if (input.controllers[0].controller == nullptr &&
            input.controllers[1].controller != nullptr) {
        input.controllers[0] = input.controllers[1];
        input.controllers[1] = {};
        std::printf("Remaining controller reassigned to Player 1.\n");
    }

    open_available_controllers_locked();
}

void close_controllers() {
    std::lock_guard lock{input.mutex};
    for (ControllerState& state : input.controllers) {
        if (state.controller != nullptr) {
            SDL_GameControllerClose(state.controller);
        }
        state = {};
    }
}

uint16_t key_button(SDL_Keycode key) {
    switch (key) {
        case SDLK_SPACE: return n64_button::a;
        case SDLK_z: return n64_button::b;
        case SDLK_x: return n64_button::z;
        case SDLK_RETURN: return n64_button::start;
        case SDLK_q: return n64_button::l;
        case SDLK_e: return n64_button::r;
        case SDLK_LEFT: return n64_button::dpad_left;
        case SDLK_RIGHT: return n64_button::dpad_right;
        case SDLK_UP: return n64_button::dpad_up;
        case SDLK_DOWN: return n64_button::dpad_down;
        case SDLK_i: return n64_button::c_up;
        case SDLK_k: return n64_button::c_down;
        case SDLK_j: return n64_button::c_left;
        case SDLK_l: return n64_button::c_right;
        default: return 0;
    }
}

uint16_t controller_button(SDL_GameControllerButton button) {
    switch (button) {
        case SDL_CONTROLLER_BUTTON_A: return n64_button::a;
        case SDL_CONTROLLER_BUTTON_B: return n64_button::b;
        case SDL_CONTROLLER_BUTTON_X: return n64_button::z;
        case SDL_CONTROLLER_BUTTON_Y: return n64_button::c_up;
        case SDL_CONTROLLER_BUTTON_START: return n64_button::start;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return n64_button::l;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return n64_button::r;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return n64_button::dpad_left;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return n64_button::dpad_right;
        case SDL_CONTROLLER_BUTTON_DPAD_UP: return n64_button::dpad_up;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return n64_button::dpad_down;
        default: return 0;
    }
}

void update_keyboard_stick() {
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    input.keyboard_x = static_cast<float>(keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT])
        - static_cast<float>(keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]);
    input.keyboard_y = static_cast<float>(keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP])
        - static_cast<float>(keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]);
}

void update_controller_state(ControllerState& state) {
    if (state.controller == nullptr) {
        state.buttons = 0;
        state.x = 0.0f;
        state.y = 0.0f;
        return;
    }

    uint16_t buttons = 0;
    for (int raw_button = SDL_CONTROLLER_BUTTON_A;
        raw_button < SDL_CONTROLLER_BUTTON_MAX;
         ++raw_button) {
        const auto button = static_cast<SDL_GameControllerButton>(raw_button);
        if (SDL_GameControllerGetButton(state.controller, button)) {
            buttons |= controller_button(button);
        }
    }

    if (SDL_GameControllerGetAxis(state.controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > trigger_threshold) {
        buttons |= n64_button::z;
    }
    if (SDL_GameControllerGetAxis(state.controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > trigger_threshold) {
        buttons |= n64_button::r;
    }

    const Sint16 right_x = SDL_GameControllerGetAxis(state.controller, SDL_CONTROLLER_AXIS_RIGHTX);
    const Sint16 right_y = SDL_GameControllerGetAxis(state.controller, SDL_CONTROLLER_AXIS_RIGHTY);
    if (right_x < -trigger_threshold) buttons |= n64_button::c_left;
    if (right_x > trigger_threshold) buttons |= n64_button::c_right;
    if (right_y < -trigger_threshold) buttons |= n64_button::c_up;
    if (right_y > trigger_threshold) buttons |= n64_button::c_down;

    state.buttons = buttons;
    state.x = normalize_axis(
        SDL_GameControllerGetAxis(state.controller, SDL_CONTROLLER_AXIS_LEFTX));
    state.y = -normalize_axis(
        SDL_GameControllerGetAxis(state.controller, SDL_CONTROLLER_AXIS_LEFTY));
}

void* create_gfx() {
    return nullptr;
}

ultramodern::renderer::WindowHandle create_window(void*) {
    const int width = current_window_width.load();
    const int height = current_window_height.load();
    Uint32 window_flags =
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
#if !defined(_WIN32)
    window_flags |= SDL_WINDOW_VULKAN;
#endif
    window = SDL_CreateWindow(
        "40 Winks PC Port",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width,
        height,
        window_flags);

    if (window == nullptr) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        ultramodern::quit();
        return {};
    }

#if defined(_WIN32)
    SDL_SysWMinfo window_info{};
    SDL_VERSION(&window_info.version);
    if (SDL_GetWindowWMInfo(window, &window_info) != SDL_TRUE ||
            window_info.subsystem != SDL_SYSWM_WINDOWS) {
        std::fprintf(stderr, "SDL could not provide the Win32 window handle: %s\n",
            SDL_GetError());
        ultramodern::quit();
        return {};
    }
    return {
        .window = window_info.info.win.window,
        .thread_id = GetCurrentThreadId(),
    };
#else
    return window;
#endif
}

void update_gfx(void*) {
    if (window != nullptr && window_resize_pending.exchange(false)) {
        const int width = pending_window_width.load();
        const int height = pending_window_height.load();
        SDL_SetWindowSize(window, width, height);
        SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        current_window_width.store(width);
        current_window_height.store(height);
    }

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            ultramodern::quit();
            continue;
        }

        if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                ultramodern::quit();
                continue;
            }

            const uint16_t mask = key_button(event.key.keysym.sym);
            std::lock_guard lock{input.mutex};
            if (event.type == SDL_KEYDOWN) {
                input.keyboard_buttons |= mask;
            } else {
                input.keyboard_buttons &= static_cast<uint16_t>(~mask);
            }
        }

        if (event.type == SDL_WINDOWEVENT &&
            event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            current_window_width.store(event.window.data1);
            current_window_height.store(event.window.data2);
        }

        if (event.type == SDL_CONTROLLERDEVICEADDED) {
            std::lock_guard lock{input.mutex};
            open_controller_device_locked(event.cdevice.which);
        }

        if (event.type == SDL_CONTROLLERDEVICEREMOVED) {
            remove_controller(event.cdevice.which);
        }
    }

    std::lock_guard lock{input.mutex};
    update_keyboard_stick();
    for (ControllerState& state : input.controllers) {
        update_controller_state(state);
    }
}

bool get_input(int controller_num, uint16_t* buttons, float* x, float* y) {
    std::lock_guard lock{input.mutex};
    const input_routing::InputSample keyboard{
        .connected = true,
        .buttons = input.keyboard_buttons,
        .x = input.keyboard_x,
        .y = input.keyboard_y,
    };
    std::array<input_routing::InputSample, input_routing::player_count> controllers{};
    for (size_t index = 0; index < controllers.size(); ++index) {
        const ControllerState& state = input.controllers[index];
        controllers[index] = {
            .connected = state.controller != nullptr,
            .buttons = state.buttons,
            .x = state.x,
            .y = state.y,
        };
    }

    const input_routing::InputSample routed =
        input_routing::route_player(controller_num, keyboard, controllers);
    if (!routed.connected) {
        return false;
    }

    *buttons = routed.buttons;
    *x = routed.x;
    *y = routed.y;
    return true;
}

void poll_input() {
    // SDL state is refreshed on the main thread by update_gfx().
}

void set_rumble(int controller_num, bool enabled) {
    if (controller_num < 0 ||
            controller_num >= static_cast<int>(input_routing::player_count)) {
        return;
    }

    std::lock_guard lock{input.mutex};
    ControllerState& state = input.controllers[static_cast<size_t>(controller_num)];
    if (state.controller != nullptr) {
        const Uint16 strength = enabled ? 0x7FFF : 0;
        SDL_GameControllerRumble(state.controller, strength, strength, enabled ? 1000 : 0);
    }
}

ultramodern::input::connected_device_info_t connected_device(int controller_num) {
    std::lock_guard lock{input.mutex};
    if (controller_num == 0) {
        // Player 1 always exists through either the first gamepad or keyboard
        // fallback. RumblePak also exposes the game-specific virtual save pak.
        return {ultramodern::input::Device::Controller,
            ultramodern::input::Pak::RumblePak};
    }

    if (controller_num != 1 || input.controllers[0].controller == nullptr) {
        return {ultramodern::input::Device::None, ultramodern::input::Pak::None};
    }

    const auto pak = input.controllers[1].controller != nullptr
        ? ultramodern::input::Pak::RumblePak
        : ultramodern::input::Pak::None;
    return {ultramodern::input::Device::Controller, pak};
}

RspExitReason discard_rsp_task(uint8_t*, uint32_t) {
    ++rsp_task_count;
    return RspExitReason::Broke;
}

RspUcodeFunc* get_rsp_microcode(const OSTask*) {
    return discard_rsp_task;
}

void queue_samples(int16_t*, size_t) {}
size_t frames_remaining() { return 0; }
void set_frequency(uint32_t) {}

void on_vi() {
    vi_seen.store(true);
}

void show_error(const char* message) {
    if (window != nullptr) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "40 Winks PC Port", message, window);
    }
}

} // namespace

bool valid_window_size(WindowSize size) {
    return size.width >= 320 && size.width <= 7680 &&
        size.height >= 240 && size.height <= 4320;
}

bool initialize(WindowSize requested_size,
                const std::filesystem::path& settings_path,
                bool use_saved_size) {
    if (!valid_window_size(requested_size)) {
        std::fprintf(stderr, "Invalid initial window size %d x %d.\n",
            requested_size.width, requested_size.height);
        return false;
    }

    {
        std::lock_guard lock{window_settings_mutex};
        window_settings_path = settings_path;
        if (use_saved_size) {
            requested_size = load_saved_window_size(requested_size);
        }
    }
    current_window_width.store(requested_size.width);
    current_window_height.store(requested_size.height);
    pending_window_width.store(requested_size.width);
    pending_window_height.store(requested_size.height);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    open_available_controllers();
    return true;
}

WindowSize current_window_size() {
    return {current_window_width.load(), current_window_height.load()};
}

bool request_window_size(WindowSize size) {
    if (!valid_window_size(size)) {
        return false;
    }

    pending_window_width.store(size.width);
    pending_window_height.store(size.height);
    window_resize_pending.store(true);
    if (!persist_window_size(size)) {
        const std::string settings_path_text = window_settings_path.string();
        std::fprintf(stderr, "Could not save display settings to %s.\n",
            settings_path_text.c_str());
        return false;
    }
    return true;
}

InputAssignments input_assignments() {
    std::lock_guard lock{input.mutex};
    return {
        .player_one_controller = input.controllers[0].controller != nullptr,
        .player_two_controller = input.controllers[1].controller != nullptr,
    };
}

void shutdown() {
    close_controllers();
    if (window != nullptr) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }
    SDL_Quit();
}

ultramodern::gfx_callbacks_t gfx_callbacks() {
    return {
        .create_gfx = create_gfx,
        .create_window = create_window,
        .update_gfx = update_gfx,
    };
}

ultramodern::renderer::callbacks_t renderer_callbacks() {
    return {.create_render_context = forty_winks::renderer::create_render_context};
}

ultramodern::input::callbacks_t input_callbacks() {
    return {
        .poll_input = poll_input,
        .get_input = get_input,
        .set_rumble = set_rumble,
        .get_connected_device_info = connected_device,
    };
}

ultramodern::audio_callbacks_t audio_callbacks() {
    return {
        .queue_samples = queue_samples,
        .get_frames_remaining = frames_remaining,
        .set_frequency = set_frequency,
    };
}

recomp::rsp::callbacks_t rsp_callbacks() {
    return {.get_rsp_microcode = get_rsp_microcode};
}

ultramodern::events::callbacks_t events_callbacks() {
    return {
        .vi_callback = on_vi,
        .gfx_init_callback = nullptr,
    };
}

ultramodern::error_handling::callbacks_t error_callbacks() {
    return {.message_box = show_error};
}

bool first_vi_seen() {
    return vi_seen.load();
}

uint64_t discarded_display_lists() {
    return forty_winks::renderer::submitted_display_lists();
}

uint64_t discarded_rsp_tasks() {
    return rsp_task_count.load();
}

} // namespace forty_winks::platform
