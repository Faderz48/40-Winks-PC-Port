#include "split_screen_patch.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <fstream>
#include <vector>

namespace forty_winks::patches {
namespace {

constexpr size_t header_crc_offset = 0x10;
constexpr size_t viewport_rom_offset = 0x87F60;
constexpr uint32_t viewport_rdram_address = 0x80087360;

constexpr std::array<uint8_t, 4> z64_magic{
    0x80, 0x37, 0x12, 0x40,
};
constexpr std::array<uint8_t, 8> patched_header_crc{
    0x5C, 0x65, 0x64, 0x60, 0x2B, 0x5B, 0x41, 0x32,
};

constexpr std::array<uint32_t, 8> original_viewports{
    0x3E8B3333, 0x3F000000, 0x3EE9999A, 0x3EE44444,
    0x3F39999A, 0x3F000000, 0x3EE9999A, 0x3EE44444,
};

// Faderz48's MIT-licensed IPS layout: two full-width, half-height panes.
constexpr std::array<uint32_t, 8> true_split_screen_viewports{
    0x3F000000, 0x3E800000, 0x3F800000, 0x3F000000,
    0x3F000000, 0x3F400000, 0x3F800000, 0x3F000000,
};

constexpr std::array<uint8_t, 32> true_split_screen_viewport_bytes{
    0x3F, 0x00, 0x00, 0x00, 0x3E, 0x80, 0x00, 0x00,
    0x3F, 0x80, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00,
    0x3F, 0x00, 0x00, 0x00, 0x3F, 0x40, 0x00, 0x00,
    0x3F, 0x80, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00,
};

std::atomic<bool> true_split_screen_enabled{true};

bool rdram_viewports_match(
        uint8_t* rdram,
        const std::array<uint32_t, 8>& expected) {
    for (size_t index = 0; index < expected.size(); ++index) {
        const uint32_t actual = static_cast<uint32_t>(
            MEM_W(index * sizeof(uint32_t), S32(viewport_rdram_address)));
        if (actual != expected[index]) {
            return false;
        }
    }
    return true;
}

bool apply_viewport_layout(
        uint8_t* rdram,
        const std::array<uint32_t, 8>& layout) {
    if (rdram == nullptr) {
        return false;
    }
    if (!rdram_viewports_match(rdram, original_viewports) &&
        !rdram_viewports_match(rdram, true_split_screen_viewports)) {
        return false;
    }

    for (size_t index = 0; index < layout.size(); ++index) {
        MEM_W(index * sizeof(uint32_t), S32(viewport_rdram_address)) =
            static_cast<int32_t>(layout[index]);
    }
    return true;
}

} // namespace

bool rom_data_uses_true_split_screen_patch(std::span<const uint8_t> rom_data) {
    const size_t required_size = viewport_rom_offset + true_split_screen_viewport_bytes.size();
    if (rom_data.size() < required_size) {
        return false;
    }

    return std::equal(z64_magic.begin(), z64_magic.end(), rom_data.begin()) &&
        std::equal(
            patched_header_crc.begin(),
            patched_header_crc.end(),
            rom_data.begin() + header_crc_offset) &&
        std::equal(
            true_split_screen_viewport_bytes.begin(),
            true_split_screen_viewport_bytes.end(),
            rom_data.begin() + viewport_rom_offset);
}

bool rom_uses_true_split_screen_patch(const std::filesystem::path& rom_path) {
    const size_t prefix_size = viewport_rom_offset + true_split_screen_viewport_bytes.size();
    std::ifstream rom{rom_path, std::ios::binary};
    if (!rom) {
        return false;
    }

    std::vector<uint8_t> prefix(prefix_size);
    rom.read(reinterpret_cast<char*>(prefix.data()),
        static_cast<std::streamsize>(prefix.size()));
    if (rom.gcount() != static_cast<std::streamsize>(prefix.size())) {
        return false;
    }
    return rom_data_uses_true_split_screen_patch(prefix);
}

void set_true_split_screen_enabled(bool enabled) {
    true_split_screen_enabled.store(enabled);
}

bool apply_true_split_screen_patch(uint8_t* rdram) {
    return apply_viewport_layout(rdram, true_split_screen_viewports);
}

void apply_startup_patches(uint8_t* rdram, recomp_context* context) {
    (void)context;
    const bool use_true_split_screen = true_split_screen_enabled.load();
    const bool applied = apply_viewport_layout(
        rdram,
        use_true_split_screen
            ? true_split_screen_viewports
            : original_viewports);
    if (applied && use_true_split_screen) {
        std::printf(
            "True split-screen enabled: Player 1 top, Player 2 bottom (Faderz48 layout).\n");
    } else if (applied) {
        std::printf("Original side-by-side split-screen enabled.\n");
    } else {
        std::fprintf(stderr,
            "Split-screen layout skipped: viewport table did not match the supported ROM.\n");
    }
}

} // namespace forty_winks::patches
