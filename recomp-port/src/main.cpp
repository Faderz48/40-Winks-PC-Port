#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>

#include "librecomp/game.hpp"
#include "ultramodern/ultramodern.hpp"

#include "overlays.hpp"
#include "platform.hpp"
#include "recomp_support.h"
#include "rt64_renderer.hpp"
#include "split_screen_patch.hpp"
#include "visual_patches.hpp"

extern "C" void recomp_entrypoint(uint8_t* rdram, recomp_context* ctx);
gpr get_entrypoint_address();

namespace {

constexpr std::u8string_view game_id = u8"40winks";

struct Options {
    std::filesystem::path rom_path;
    std::filesystem::path data_path;
    forty_winks::platform::WindowSize window_size =
        forty_winks::platform::default_window_size;
    forty_winks::renderer::DisplayAspectMode aspect_mode =
        forty_winks::renderer::DisplayAspectMode::HybridWidescreen;
    bool true_split_screen = true;
    bool resolution_explicit = false;
    unsigned smoke_seconds = 0;
};

std::filesystem::path default_data_path() {
#if defined(_WIN32)
    if (const char* local_app_data = std::getenv("LOCALAPPDATA")) {
        return std::filesystem::path{local_app_data} / "40-winks-pc-port";
    }
    if (const char* app_data = std::getenv("APPDATA")) {
        return std::filesystem::path{app_data} / "40-winks-pc-port";
    }
#endif
    if (const char* xdg_data = std::getenv("XDG_DATA_HOME")) {
        return std::filesystem::path{xdg_data} / "40-winks-pc-port";
    }
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path{home} / ".local/share/40-winks-pc-port";
    }
    return std::filesystem::current_path() / "40-winks-data";
}

void print_usage(const char* executable) {
    std::fprintf(stderr,
        "Usage: %s --rom <40-winks.z64> [--data-dir <path>] "
        "[--resolution <width>x<height>] "
        "[--aspect <hybrid|original|stretch>] [--original-split-screen] "
        "[--smoke-seconds <n>]\n",
        executable);
}

bool parse_aspect_mode(
    std::string_view text,
    forty_winks::renderer::DisplayAspectMode& mode) {
    if (text == "hybrid") {
        mode = forty_winks::renderer::DisplayAspectMode::HybridWidescreen;
        return true;
    }
    if (text == "original" || text == "4:3") {
        mode = forty_winks::renderer::DisplayAspectMode::Original4x3;
        return true;
    }
    if (text == "stretch") {
        mode = forty_winks::renderer::DisplayAspectMode::Stretch;
        return true;
    }
    return false;
}

bool parse_resolution(std::string_view text,
                      forty_winks::platform::WindowSize& size) {
    const size_t separator = text.find_first_of("xX");
    if (separator == std::string_view::npos || separator == 0 ||
        separator + 1 >= text.size()) {
        return false;
    }

    int width = 0;
    int height = 0;
    const auto width_result = std::from_chars(
        text.data(), text.data() + separator, width);
    const auto height_result = std::from_chars(
        text.data() + separator + 1, text.data() + text.size(), height);
    if (width_result.ec != std::errc{} ||
        width_result.ptr != text.data() + separator ||
        height_result.ec != std::errc{} ||
        height_result.ptr != text.data() + text.size()) {
        return false;
    }

    const forty_winks::platform::WindowSize parsed{width, height};
    if (!forty_winks::platform::valid_window_size(parsed)) {
        return false;
    }
    size = parsed;
    return true;
}

bool parse_options(int argc, char** argv, Options& options) {
    options.data_path = default_data_path();
    if (const char* rom_env = std::getenv("FORTY_WINKS_ROM")) {
        options.rom_path = rom_env;
    }
    if (const char* resolution_env = std::getenv("FORTY_WINKS_RESOLUTION")) {
        if (!parse_resolution(resolution_env, options.window_size)) {
            std::fprintf(stderr, "Invalid FORTY_WINKS_RESOLUTION: %s\n",
                resolution_env);
            return false;
        }
        options.resolution_explicit = true;
    }

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--rom" && index + 1 < argc) {
            options.rom_path = argv[++index];
        } else if (argument == "--data-dir" && index + 1 < argc) {
            options.data_path = argv[++index];
        } else if (argument == "--resolution" && index + 1 < argc) {
            const char* resolution = argv[++index];
            if (!parse_resolution(resolution, options.window_size)) {
                std::fprintf(stderr,
                    "Invalid resolution: %s (supported range 320x240 to 7680x4320)\n",
                    resolution);
                return false;
            }
            options.resolution_explicit = true;
        } else if (argument == "--aspect" && index + 1 < argc) {
            const char* aspect = argv[++index];
            if (!parse_aspect_mode(aspect, options.aspect_mode)) {
                std::fprintf(stderr,
                    "Invalid aspect mode: %s (expected hybrid, original, or stretch)\n",
                    aspect);
                return false;
            }
        } else if (argument == "--original-split-screen") {
            options.true_split_screen = false;
        } else if (argument == "--smoke-seconds" && index + 1 < argc) {
            options.smoke_seconds = static_cast<unsigned>(std::strtoul(argv[++index], nullptr, 10));
        } else if (argument == "--help" || argument == "-h") {
            print_usage(argv[0]);
            return false;
        } else {
            std::fprintf(stderr, "Unknown or incomplete option: %s\n", argument.c_str());
            print_usage(argv[0]);
            return false;
        }
    }

    if (options.rom_path.empty()) {
        std::fprintf(stderr, "No ROM was selected. Use --rom or FORTY_WINKS_ROM.\n");
        print_usage(argv[0]);
        return false;
    }
    return true;
}

const char* validation_error(recomp::RomValidationError error) {
    switch (error) {
        case recomp::RomValidationError::Good: return "good";
        case recomp::RomValidationError::FailedToOpen: return "could not open the ROM";
        case recomp::RomValidationError::NotARom: return "file is not an N64 ROM";
        case recomp::RomValidationError::IncorrectRom: return "ROM is not 40 Winks";
        case recomp::RomValidationError::NotYet: return "ROM revision is not supported yet";
        case recomp::RomValidationError::IncorrectVersion: return "ROM revision does not match";
        case recomp::RomValidationError::OtherError: return "runtime rejected the ROM";
    }
    return "unknown validation error";
}

void apply_game_startup_patches(uint8_t* rdram, recomp_context* context) {
    forty_winks::patches::apply_startup_patches(rdram, context);
    if (forty_winks::patches::apply_reduced_lightning_flash_patch(rdram)) {
        std::printf("Reduced full-screen lightning flashes enabled.\n");
    } else {
        std::fprintf(stderr,
            "Reduced lightning flash patch skipped: color table did not match.\n");
    }
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parse_options(argc, argv, options)) {
        return EXIT_FAILURE;
    }

    std::error_code path_error;
    std::filesystem::create_directories(options.data_path, path_error);
    if (path_error) {
        const std::string data_path_text = options.data_path.string();
        std::fprintf(stderr, "Could not create data directory %s: %s\n",
            data_path_text.c_str(), path_error.message().c_str());
        return EXIT_FAILURE;
    }

    const std::string data_path_text = options.data_path.string();
    configure_controller_pak_storage(data_path_text.c_str());
    forty_winks::renderer::set_display_aspect_mode(options.aspect_mode);
    forty_winks::patches::set_true_split_screen_enabled(
        options.true_split_screen);

    if (!forty_winks::platform::initialize(
            options.window_size,
            options.data_path / "display-settings.cfg",
            !options.resolution_explicit)) {
        return EXIT_FAILURE;
    }

    recomp::Version version;
    if (!recomp::Version::from_string("0.1.0", version)) {
        std::fprintf(stderr, "Invalid project version.\n");
        forty_winks::platform::shutdown();
        return EXIT_FAILURE;
    }

    const bool patched_rom =
        forty_winks::patches::rom_uses_true_split_screen_patch(options.rom_path);
    const uint64_t expected_rom_hash = patched_rom
        ? forty_winks::patches::true_split_screen_rom_hash
        : forty_winks::patches::clean_rom_hash;

    const recomp::GameEntry game{
        .rom_hash = expected_rom_hash,
        .internal_name = "40 WINKS            ",
        .game_id = std::u8string{game_id},
        .mod_game_id = "",
        .save_type = recomp::SaveType::None,
        .is_enabled = true,
        .entrypoint_address = get_entrypoint_address(),
        .entrypoint = recomp_entrypoint,
        .on_init_callback = apply_game_startup_patches,
    };

    recomp::register_config_path(options.data_path);
    recomp::register_game(game);
    register_game_overlays();

    std::u8string selected_game_id{game_id};
    const recomp::RomValidationError rom_result =
        recomp::select_rom(options.rom_path, selected_game_id);
    if (rom_result != recomp::RomValidationError::Good) {
        std::fprintf(stderr, "ROM validation failed: %s\n", validation_error(rom_result));
        forty_winks::platform::shutdown();
        return EXIT_FAILURE;
    }

    std::printf("ROM validated. Preparing recompiled 40 Winks CPU at 0x%08X.\n",
        static_cast<uint32_t>(game.entrypoint_address));
    if (patched_rom) {
        std::printf("Faderz48 true split-screen IPS ROM detected and accepted.\n");
    }

    std::atomic<bool> runtime_finished = false;
    std::thread runtime_control{
        [&runtime_finished, selected_game_id, smoke_seconds = options.smoke_seconds]() {
            while (!runtime_finished.load() && !forty_winks::platform::first_vi_seen()) {
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }
            if (runtime_finished.load()) {
                return;
            }

            std::printf("Runtime video initialized. Starting game CPU.\n");
            recomp::start_game(selected_game_id);

            if (smoke_seconds == 0) {
                return;
            }

            const auto deadline =
                std::chrono::steady_clock::now() + std::chrono::seconds{smoke_seconds};
            while (!runtime_finished.load() && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds{10});
            }
            if (!runtime_finished.load()) {
                ultramodern::quit();
            }
        }};

    const recomp::Configuration config{
        .project_version = version,
        .window_handle = nullptr,
        .rsp_callbacks = forty_winks::platform::rsp_callbacks(),
        .renderer_callbacks = forty_winks::platform::renderer_callbacks(),
        .audio_callbacks = forty_winks::platform::audio_callbacks(),
        .input_callbacks = forty_winks::platform::input_callbacks(),
        .gfx_callbacks = forty_winks::platform::gfx_callbacks(),
        .events_callbacks = forty_winks::platform::events_callbacks(),
        .error_handling_callbacks = forty_winks::platform::error_callbacks(),
    };

    recomp::start(config);
    runtime_finished.store(true);

    runtime_control.join();

    std::printf("Runtime stopped after %llu display lists and %llu audio RSP tasks.\n",
        static_cast<unsigned long long>(forty_winks::platform::discarded_display_lists()),
        static_cast<unsigned long long>(forty_winks::platform::processed_audio_rsp_tasks()));

    forty_winks::platform::shutdown();
    return EXIT_SUCCESS;
}
