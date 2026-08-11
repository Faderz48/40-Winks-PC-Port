#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace forty_winks::input_routing {

constexpr std::size_t player_count = 2;

struct InputSample {
    bool connected = false;
    uint16_t buttons = 0;
    float x = 0.0f;
    float y = 0.0f;
};

InputSample route_player(
    int player,
    const InputSample& keyboard,
    const std::array<InputSample, player_count>& controllers);

} // namespace forty_winks::input_routing
