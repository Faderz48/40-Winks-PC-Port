#pragma once

#include <cstdint>

namespace forty_winks::patches {

inline constexpr uint32_t lightning_flash_color_table_address = 0x8008266C;
inline constexpr uint8_t reduced_lightning_peak_alpha = 0xA0;

bool apply_reduced_lightning_flash_patch(uint8_t* rdram);

} // namespace forty_winks::patches
