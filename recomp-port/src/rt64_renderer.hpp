#pragma once

#include <cstdint>
#include <memory>

#include "ultramodern/renderer_context.hpp"

namespace forty_winks::renderer {

enum class DisplayAspectMode : uint8_t {
    HybridWidescreen,
    Original4x3,
    Stretch,
};

std::unique_ptr<ultramodern::renderer::RendererContext> create_render_context(
    uint8_t* rdram,
    ultramodern::renderer::WindowHandle window_handle,
    bool developer_mode);

uint64_t submitted_display_lists();
DisplayAspectMode display_aspect_mode();
void set_display_aspect_mode(DisplayAspectMode mode);

} // namespace forty_winks::renderer
