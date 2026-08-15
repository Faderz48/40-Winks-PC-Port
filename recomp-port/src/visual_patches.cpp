#include "visual_patches.hpp"

#include <array>

#include "recomp.h"

namespace forty_winks::patches {
namespace {

// func_80015798 uses these RGBA entries for the outdoor lightning overlay.
constexpr std::array<uint8_t, 20> original_lightning_colors{
    0x10, 0x10, 0x10, 0x10,
    0x30, 0x30, 0x30, 0x30,
    0xFF, 0xFF, 0xF0, 0xFF,
    0xC0, 0xC0, 0xA0, 0xB5,
    0x80, 0x80, 0x60, 0x75,
};

constexpr size_t peak_alpha_index = 11;

bool table_matches(
        uint8_t* rdram,
        const std::array<uint8_t, original_lightning_colors.size()>& expected) {
    for (size_t index = 0; index < expected.size(); ++index) {
        if (MEM_BU(static_cast<int32_t>(index),
                S32(lightning_flash_color_table_address)) != expected[index]) {
            return false;
        }
    }
    return true;
}

} // namespace

bool apply_reduced_lightning_flash_patch(uint8_t* rdram) {
    if (rdram == nullptr) {
        return false;
    }

    auto reduced_colors = original_lightning_colors;
    reduced_colors[peak_alpha_index] = reduced_lightning_peak_alpha;
    if (!table_matches(rdram, original_lightning_colors) &&
        !table_matches(rdram, reduced_colors)) {
        return false;
    }

    MEM_B(static_cast<int32_t>(peak_alpha_index),
        S32(lightning_flash_color_table_address)) =
        static_cast<int8_t>(reduced_lightning_peak_alpha);
    return true;
}

} // namespace forty_winks::patches
