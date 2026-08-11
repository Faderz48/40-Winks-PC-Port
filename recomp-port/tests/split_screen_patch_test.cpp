#include "split_screen_patch.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

constexpr uint32_t viewport_address = 0x80087360;
constexpr size_t viewport_rom_offset = 0x87F60;

constexpr std::array<uint32_t, 8> original_viewports{
    0x3E8B3333, 0x3F000000, 0x3EE9999A, 0x3EE44444,
    0x3F39999A, 0x3F000000, 0x3EE9999A, 0x3EE44444,
};

constexpr std::array<uint32_t, 8> patched_viewports{
    0x3F000000, 0x3E800000, 0x3F800000, 0x3F000000,
    0x3F000000, 0x3F400000, 0x3F800000, 0x3F000000,
};

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
    }
    return condition;
}

void write_viewports(uint8_t* rdram, const std::array<uint32_t, 8>& viewports) {
    for (size_t index = 0; index < viewports.size(); ++index) {
        MEM_W(index * sizeof(uint32_t), S32(viewport_address)) =
            static_cast<int32_t>(viewports[index]);
    }
}

bool viewports_equal(uint8_t* rdram, const std::array<uint32_t, 8>& expected) {
    for (size_t index = 0; index < expected.size(); ++index) {
        if (static_cast<uint32_t>(
                MEM_W(index * sizeof(uint32_t), S32(viewport_address))) !=
            expected[index]) {
            return false;
        }
    }
    return true;
}

} // namespace

int main() {
    bool passed = true;

    std::vector<uint8_t> memory(8 * 1024 * 1024);
    uint8_t* rdram = memory.data();
    passed &= expect(
        !forty_winks::patches::apply_true_split_screen_patch(rdram),
        "an unknown viewport table should not be modified");

    write_viewports(rdram, original_viewports);
    passed &= expect(
        forty_winks::patches::apply_true_split_screen_patch(rdram),
        "the clean viewport table should be accepted");
    passed &= expect(
        viewports_equal(rdram, patched_viewports),
        "the clean viewport table should become top/bottom split-screen");
    passed &= expect(
        forty_winks::patches::apply_true_split_screen_patch(rdram),
        "applying the patch twice should be safe");

    std::vector<uint8_t> patched_rom(viewport_rom_offset + 32);
    patched_rom[0] = 0x80;
    patched_rom[1] = 0x37;
    patched_rom[2] = 0x12;
    patched_rom[3] = 0x40;
    const std::array<uint8_t, 8> patched_crc{
        0x5C, 0x65, 0x64, 0x60, 0x2B, 0x5B, 0x41, 0x32,
    };
    for (size_t index = 0; index < patched_crc.size(); ++index) {
        patched_rom[0x10 + index] = patched_crc[index];
    }
    for (size_t index = 0; index < patched_viewports.size(); ++index) {
        const uint32_t value = patched_viewports[index];
        const size_t offset = viewport_rom_offset + index * sizeof(uint32_t);
        patched_rom[offset + 0] = static_cast<uint8_t>(value >> 24);
        patched_rom[offset + 1] = static_cast<uint8_t>(value >> 16);
        patched_rom[offset + 2] = static_cast<uint8_t>(value >> 8);
        patched_rom[offset + 3] = static_cast<uint8_t>(value);
    }
    passed &= expect(
        forty_winks::patches::rom_data_uses_true_split_screen_patch(patched_rom),
        "the upstream IPS byte pattern should be detected");
    patched_rom[viewport_rom_offset] ^= 1;
    passed &= expect(
        !forty_winks::patches::rom_data_uses_true_split_screen_patch(patched_rom),
        "a changed viewport byte should reject patched-ROM detection");

    return passed ? 0 : 1;
}
