#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "recomp.h"
#include "visual_patches.hpp"

namespace {

constexpr std::array<uint8_t, 20> original_lightning_colors{
    0x10, 0x10, 0x10, 0x10,
    0x30, 0x30, 0x30, 0x30,
    0xFF, 0xFF, 0xF0, 0xFF,
    0xC0, 0xC0, 0xA0, 0xB5,
    0x80, 0x80, 0x60, 0x75,
};

void write_table(uint8_t* rdram, const std::array<uint8_t, 20>& colors) {
    for (size_t index = 0; index < colors.size(); ++index) {
        MEM_B(static_cast<int32_t>(index),
            S32(forty_winks::patches::lightning_flash_color_table_address)) =
            static_cast<int8_t>(colors[index]);
    }
}

uint8_t read_color(uint8_t* rdram, size_t index) {
    return MEM_BU(static_cast<int32_t>(index),
        S32(forty_winks::patches::lightning_flash_color_table_address));
}

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
    }
    return condition;
}

} // namespace

int main() {
    std::vector<uint8_t> memory(8 * 1024 * 1024);
    uint8_t* rdram = memory.data();
    bool passed = true;

    write_table(rdram, original_lightning_colors);
    passed &= expect(
        forty_winks::patches::apply_reduced_lightning_flash_patch(rdram),
        "the known lightning table should be patched");
    passed &= expect(
        read_color(rdram, 11) ==
            forty_winks::patches::reduced_lightning_peak_alpha,
        "the opaque lightning peak should be reduced");
    for (size_t index = 0; index < original_lightning_colors.size(); ++index) {
        if (index != 11) {
            passed &= expect(
                read_color(rdram, index) == original_lightning_colors[index],
                "non-peak lightning colors should remain unchanged");
        }
    }

    passed &= expect(
        forty_winks::patches::apply_reduced_lightning_flash_patch(rdram),
        "applying the visual patch twice should be safe");

    write_table(rdram, original_lightning_colors);
    MEM_B(0, S32(forty_winks::patches::lightning_flash_color_table_address)) = 0;
    passed &= expect(
        !forty_winks::patches::apply_reduced_lightning_flash_patch(rdram),
        "an unknown color table should be rejected");
    passed &= expect(
        !forty_winks::patches::apply_reduced_lightning_flash_patch(nullptr),
        "a null RDRAM pointer should be rejected");

    return passed ? 0 : 1;
}
