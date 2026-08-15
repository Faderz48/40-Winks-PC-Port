#pragma once

#include <cstdint>
#include <filesystem>
#include <span>

#include "recomp.h"

namespace forty_winks::patches {

inline constexpr uint64_t clean_rom_hash = 0xB75D6703D04BCAC0ULL;
inline constexpr uint64_t true_split_screen_rom_hash = 0xC79C10F50A3996D8ULL;

bool rom_data_uses_true_split_screen_patch(std::span<const uint8_t> rom_data);
bool rom_uses_true_split_screen_patch(const std::filesystem::path& rom_path);

void set_true_split_screen_enabled(bool enabled);
bool apply_true_split_screen_patch(uint8_t* rdram);
void apply_startup_patches(uint8_t* rdram, recomp_context* context);

} // namespace forty_winks::patches
