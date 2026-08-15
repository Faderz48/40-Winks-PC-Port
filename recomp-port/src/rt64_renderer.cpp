#include "rt64_renderer.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <memory>

#include "hle/rt64_application.h"
#include "ultramodern/config.hpp"

#include "debug_menu.hpp"

namespace forty_winks::renderer {
namespace {

std::atomic<uint64_t> display_list_count = 0;
std::atomic<DisplayAspectMode> requested_display_aspect_mode{
    DisplayAspectMode::HybridWidescreen,
};

uint8_t rom_header[0x40]{};
uint8_t dmem[0x1000]{};
uint8_t imem[0x1000]{};

uint32_t mi_intr_reg = 0;
uint32_t dpc_start_reg = 0;
uint32_t dpc_end_reg = 0;
uint32_t dpc_current_reg = 0;
uint32_t dpc_status_reg = 0;
uint32_t dpc_clock_reg = 0;
uint32_t dpc_bufbusy_reg = 0;
uint32_t dpc_pipebusy_reg = 0;
uint32_t dpc_tmem_reg = 0;

void check_interrupts() {}

RT64::UserConfiguration::AspectRatio to_rt64(
    ultramodern::renderer::AspectRatio value) {
    switch (value) {
        case ultramodern::renderer::AspectRatio::Expand:
            return RT64::UserConfiguration::AspectRatio::Expand;
        case ultramodern::renderer::AspectRatio::Manual:
            return RT64::UserConfiguration::AspectRatio::Manual;
        case ultramodern::renderer::AspectRatio::Original:
        case ultramodern::renderer::AspectRatio::OptionCount:
            return RT64::UserConfiguration::AspectRatio::Original;
    }
    return RT64::UserConfiguration::AspectRatio::Original;
}

RT64::UserConfiguration::Antialiasing to_rt64(
    ultramodern::renderer::Antialiasing value) {
    switch (value) {
        case ultramodern::renderer::Antialiasing::MSAA2X:
            return RT64::UserConfiguration::Antialiasing::MSAA2X;
        case ultramodern::renderer::Antialiasing::MSAA4X:
            return RT64::UserConfiguration::Antialiasing::MSAA4X;
        case ultramodern::renderer::Antialiasing::MSAA8X:
            return RT64::UserConfiguration::Antialiasing::MSAA8X;
        case ultramodern::renderer::Antialiasing::None:
        case ultramodern::renderer::Antialiasing::OptionCount:
            return RT64::UserConfiguration::Antialiasing::None;
    }
    return RT64::UserConfiguration::Antialiasing::None;
}

RT64::UserConfiguration::RefreshRate to_rt64(
    ultramodern::renderer::RefreshRate value) {
    switch (value) {
        case ultramodern::renderer::RefreshRate::Display:
            return RT64::UserConfiguration::RefreshRate::Display;
        case ultramodern::renderer::RefreshRate::Manual:
            return RT64::UserConfiguration::RefreshRate::Manual;
        case ultramodern::renderer::RefreshRate::Original:
        case ultramodern::renderer::RefreshRate::OptionCount:
            return RT64::UserConfiguration::RefreshRate::Original;
    }
    return RT64::UserConfiguration::RefreshRate::Original;
}

RT64::UserConfiguration::InternalColorFormat to_rt64(
    ultramodern::renderer::HighPrecisionFramebuffer value) {
    switch (value) {
        case ultramodern::renderer::HighPrecisionFramebuffer::On:
            return RT64::UserConfiguration::InternalColorFormat::High;
        case ultramodern::renderer::HighPrecisionFramebuffer::Off:
            return RT64::UserConfiguration::InternalColorFormat::Standard;
        case ultramodern::renderer::HighPrecisionFramebuffer::Auto:
        case ultramodern::renderer::HighPrecisionFramebuffer::OptionCount:
            return RT64::UserConfiguration::InternalColorFormat::Automatic;
    }
    return RT64::UserConfiguration::InternalColorFormat::Automatic;
}

RT64::UserConfiguration::GraphicsAPI to_rt64(
    ultramodern::renderer::GraphicsApi value) {
    switch (value) {
        case ultramodern::renderer::GraphicsApi::D3D12:
            return RT64::UserConfiguration::GraphicsAPI::D3D12;
        case ultramodern::renderer::GraphicsApi::Vulkan:
            return RT64::UserConfiguration::GraphicsAPI::Vulkan;
        case ultramodern::renderer::GraphicsApi::Metal:
            return RT64::UserConfiguration::GraphicsAPI::Metal;
        case ultramodern::renderer::GraphicsApi::Auto:
        case ultramodern::renderer::GraphicsApi::OptionCount:
            return RT64::UserConfiguration::GraphicsAPI::Automatic;
    }
    return RT64::UserConfiguration::GraphicsAPI::Automatic;
}

ultramodern::renderer::GraphicsApi from_rt64(
    RT64::UserConfiguration::GraphicsAPI value) {
    switch (value) {
        case RT64::UserConfiguration::GraphicsAPI::D3D12:
            return ultramodern::renderer::GraphicsApi::D3D12;
        case RT64::UserConfiguration::GraphicsAPI::Vulkan:
            return ultramodern::renderer::GraphicsApi::Vulkan;
        case RT64::UserConfiguration::GraphicsAPI::Metal:
            return ultramodern::renderer::GraphicsApi::Metal;
        case RT64::UserConfiguration::GraphicsAPI::Automatic:
        case RT64::UserConfiguration::GraphicsAPI::OptionCount:
            return ultramodern::renderer::GraphicsApi::Auto;
    }
    return ultramodern::renderer::GraphicsApi::Auto;
}

ultramodern::renderer::SetupResult from_rt64(RT64::Application::SetupResult value) {
    switch (value) {
        case RT64::Application::SetupResult::Success:
            return ultramodern::renderer::SetupResult::Success;
        case RT64::Application::SetupResult::DynamicLibrariesNotFound:
            return ultramodern::renderer::SetupResult::DynamicLibrariesNotFound;
        case RT64::Application::SetupResult::InvalidGraphicsAPI:
            return ultramodern::renderer::SetupResult::InvalidGraphicsAPI;
        case RT64::Application::SetupResult::GraphicsAPINotFound:
            return ultramodern::renderer::SetupResult::GraphicsAPINotFound;
        case RT64::Application::SetupResult::GraphicsDeviceNotFound:
            return ultramodern::renderer::SetupResult::GraphicsDeviceNotFound;
    }
    return ultramodern::renderer::SetupResult::GraphicsDeviceNotFound;
}

void apply_config(
    RT64::Application& app,
    const ultramodern::renderer::GraphicsConfig& config) {
    const int downsample = std::max(config.ds_option, 1);
    switch (config.res_option) {
        case ultramodern::renderer::Resolution::Original:
            app.userConfig.resolution = RT64::UserConfiguration::Resolution::Manual;
            app.userConfig.resolutionMultiplier = downsample;
            app.userConfig.downsampleMultiplier = downsample;
            break;
        case ultramodern::renderer::Resolution::Original2x:
            app.userConfig.resolution = RT64::UserConfiguration::Resolution::Manual;
            app.userConfig.resolutionMultiplier = 2.0 * downsample;
            app.userConfig.downsampleMultiplier = downsample;
            break;
        case ultramodern::renderer::Resolution::Auto:
        case ultramodern::renderer::Resolution::OptionCount:
            app.userConfig.resolution = RT64::UserConfiguration::Resolution::WindowIntegerScale;
            app.userConfig.downsampleMultiplier = 1;
            break;
    }

    switch (config.hr_option) {
        case ultramodern::renderer::HUDRatioMode::Clamp16x9:
            app.userConfig.extAspectRatio = RT64::UserConfiguration::AspectRatio::Manual;
            app.userConfig.extAspectTarget = 16.0 / 9.0;
            break;
        case ultramodern::renderer::HUDRatioMode::Full:
            app.userConfig.extAspectRatio = RT64::UserConfiguration::AspectRatio::Expand;
            break;
        case ultramodern::renderer::HUDRatioMode::Original:
        case ultramodern::renderer::HUDRatioMode::OptionCount:
            app.userConfig.extAspectRatio = RT64::UserConfiguration::AspectRatio::Original;
            break;
    }

    app.userConfig.graphicsAPI = to_rt64(config.api_option);
    app.userConfig.aspectRatio = to_rt64(config.ar_option);
    app.userConfig.antialiasing = to_rt64(config.msaa_option);
    app.userConfig.refreshRate = to_rt64(config.rr_option);
    app.userConfig.refreshRateTarget = config.rr_manual_value;
    app.userConfig.internalColorFormat = to_rt64(config.hpfb_option);
    app.userConfig.displayBuffering = RT64::UserConfiguration::DisplayBuffering::Triple;
    app.userConfig.filtering = RT64::UserConfiguration::Filtering::AntiAliasedPixelScaling;
}

void apply_port_display_mode(
    RT64::Application& app,
    DisplayAspectMode mode) {
    app.userConfig.resolution =
        RT64::UserConfiguration::Resolution::WindowIntegerScale;
    app.userConfig.downsampleMultiplier = 1;
    app.userConfig.aspectRatio = mode == DisplayAspectMode::HybridWidescreen
        ? RT64::UserConfiguration::AspectRatio::Expand
        : RT64::UserConfiguration::AspectRatio::Original;
    app.userConfig.extAspectRatio = RT64::UserConfiguration::AspectRatio::Original;
}

class RT64Context final : public ultramodern::renderer::RendererContext {
public:
    RT64Context(
        uint8_t* rdram,
        ultramodern::renderer::WindowHandle window_handle,
        bool developer_mode) {
        RT64::Application::Core core{};
#if defined(_WIN32)
        core.window = window_handle.window;
        const uint32_t window_thread_id = window_handle.thread_id;
#else
        core.window = window_handle;
        const uint32_t window_thread_id = 0;
#endif
        core.checkInterrupts = check_interrupts;
        core.HEADER = rom_header;
        core.RDRAM = rdram;
        core.DMEM = dmem;
        core.IMEM = imem;
        core.MI_INTR_REG = &mi_intr_reg;
        core.DPC_START_REG = &dpc_start_reg;
        core.DPC_END_REG = &dpc_end_reg;
        core.DPC_CURRENT_REG = &dpc_current_reg;
        core.DPC_STATUS_REG = &dpc_status_reg;
        core.DPC_CLOCK_REG = &dpc_clock_reg;
        core.DPC_BUFBUSY_REG = &dpc_bufbusy_reg;
        core.DPC_PIPEBUSY_REG = &dpc_pipebusy_reg;
        core.DPC_TMEM_REG = &dpc_tmem_reg;

        ultramodern::renderer::ViRegs* vi = ultramodern::renderer::get_vi_regs();
        core.VI_STATUS_REG = &vi->VI_STATUS_REG;
        core.VI_ORIGIN_REG = &vi->VI_ORIGIN_REG;
        core.VI_WIDTH_REG = &vi->VI_WIDTH_REG;
        core.VI_INTR_REG = &vi->VI_INTR_REG;
        core.VI_V_CURRENT_LINE_REG = &vi->VI_V_CURRENT_LINE_REG;
        core.VI_TIMING_REG = &vi->VI_TIMING_REG;
        core.VI_V_SYNC_REG = &vi->VI_V_SYNC_REG;
        core.VI_H_SYNC_REG = &vi->VI_H_SYNC_REG;
        core.VI_LEAP_REG = &vi->VI_LEAP_REG;
        core.VI_H_START_REG = &vi->VI_H_START_REG;
        core.VI_V_START_REG = &vi->VI_V_START_REG;
        core.VI_V_BURST_REG = &vi->VI_V_BURST_REG;
        core.VI_X_SCALE_REG = &vi->VI_X_SCALE_REG;
        core.VI_Y_SCALE_REG = &vi->VI_Y_SCALE_REG;

        RT64::ApplicationConfiguration app_config;
        app_config.appId = "40-winks-pc-port";
        app_config.useConfigurationFile = false;
        app_config.forceAspectCorrectTransforms = true;
        app_config.preserveRectangleAspect = false;
        app_config.cpuFramebufferAspectRatio = 4.0f / 3.0f;
        app_config.stretchFramebuffersToWindow = [] {
            return requested_display_aspect_mode.load() ==
                DisplayAspectMode::Stretch;
        };
        app_config.drawUserInterface = forty_winks::debug_menu::draw;

        app = std::make_unique<RT64::Application>(core, app_config);
        const auto& config = ultramodern::renderer::get_graphics_config();
        apply_config(*app, config);
        applied_display_aspect_mode = requested_display_aspect_mode.load();
        apply_port_display_mode(*app, applied_display_aspect_mode);
        app->userConfig.developerMode =
            developer_mode || static_cast<bool>(app_config.drawUserInterface);

        setup_result = from_rt64(app->setup(window_thread_id));
        chosen_api = from_rt64(app->chosenGraphicsAPI);
        if (setup_result != ultramodern::renderer::SetupResult::Success) {
            std::fprintf(stderr, "RT64 failed to initialize (result %d).\n",
                static_cast<int>(setup_result));
            app.reset();
            return;
        }

        app->setFullScreen(config.wm_option == ultramodern::renderer::WindowMode::Fullscreen);
        std::printf("RT64 initialized with graphics API %d.\n",
            static_cast<int>(chosen_api));
    }

    bool valid() override { return app != nullptr; }

    bool update_config(
        const ultramodern::renderer::GraphicsConfig& old_config,
        const ultramodern::renderer::GraphicsConfig& new_config) override {
        if (app == nullptr || old_config == new_config) {
            return false;
        }

        if (old_config.wm_option != new_config.wm_option) {
            app->setFullScreen(
                new_config.wm_option == ultramodern::renderer::WindowMode::Fullscreen);
        }

        apply_config(*app, new_config);
        apply_port_display_mode(*app, applied_display_aspect_mode);
        app->updateUserConfig(true);
        if (old_config.msaa_option != new_config.msaa_option) {
            app->updateMultisampling();
        }
        return true;
    }

    void enable_instant_present() override {
        if (app == nullptr) {
            return;
        }
        app->enhancementConfig.presentation.mode =
            RT64::EnhancementConfiguration::Presentation::Mode::PresentEarly;
        app->updateEnhancementConfig();
    }

    void send_dl(const OSTask* task) override {
        if (app == nullptr || task == nullptr) {
            return;
        }

        ++display_list_count;

        app->state->rsp->reset();
        app->interpreter->loadUCodeGBI(
            task->t.ucode & 0x03FFFFFF,
            task->t.ucode_data & 0x03FFFFFF,
            true);
        app->processDisplayLists(
            app->core.RDRAM,
            task->t.data_ptr & 0x03FFFFFF,
            0,
            true);
    }

    void update_screen() override {
        if (app != nullptr) {
            const DisplayAspectMode requested_mode =
                requested_display_aspect_mode.load();
            if (requested_mode != applied_display_aspect_mode) {
                applied_display_aspect_mode = requested_mode;
                apply_port_display_mode(*app, applied_display_aspect_mode);
                app->updateUserConfig(true);
            }
            app->updateScreen();
        }
    }

    void shutdown() override {
        if (app != nullptr) {
            app->end();
        }
    }

    uint32_t get_display_framerate() const override {
        if (app == nullptr || app->presentQueue == nullptr) {
            return 60;
        }
        return app->presentQueue->ext.sharedResources->swapChainRate;
    }

    float get_resolution_scale() const override {
        if (app == nullptr) {
            return 1.0f;
        }

        constexpr int reference_height = 240;
        if (app->userConfig.resolution ==
            RT64::UserConfiguration::Resolution::WindowIntegerScale) {
            const uint32_t height = app->sharedQueueResources->swapChainHeight;
            if (height > 0) {
                return std::max(
                    static_cast<float>((height + reference_height - 1) / reference_height),
                    1.0f);
            }
            return 1.0f;
        }

        if (app->userConfig.resolution == RT64::UserConfiguration::Resolution::Manual) {
            return static_cast<float>(app->userConfig.resolutionMultiplier);
        }
        return 1.0f;
    }

private:
    std::unique_ptr<RT64::Application> app;
    DisplayAspectMode applied_display_aspect_mode =
        DisplayAspectMode::HybridWidescreen;
};

} // namespace

std::unique_ptr<ultramodern::renderer::RendererContext> create_render_context(
    uint8_t* rdram,
    ultramodern::renderer::WindowHandle window_handle,
    bool developer_mode) {
    return std::make_unique<RT64Context>(rdram, window_handle, developer_mode);
}

uint64_t submitted_display_lists() {
    return display_list_count.load();
}

DisplayAspectMode display_aspect_mode() {
    return requested_display_aspect_mode.load();
}

void set_display_aspect_mode(DisplayAspectMode mode) {
    requested_display_aspect_mode.store(mode);
}

} // namespace forty_winks::renderer
