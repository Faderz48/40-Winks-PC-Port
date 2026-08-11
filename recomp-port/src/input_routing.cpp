#include "input_routing.hpp"

#include <cmath>

namespace forty_winks::input_routing {
namespace {

InputSample merge_sources(const InputSample& first, const InputSample& second) {
    if (!first.connected) {
        return second;
    }
    if (!second.connected) {
        return first;
    }

    return {
        .connected = true,
        .buttons = static_cast<uint16_t>(first.buttons | second.buttons),
        .x = std::abs(second.x) > std::abs(first.x) ? second.x : first.x,
        .y = std::abs(second.y) > std::abs(first.y) ? second.y : first.y,
    };
}

} // namespace

InputSample route_player(
    int player,
    const InputSample& keyboard,
    const std::array<InputSample, player_count>& controllers) {
    if (player == 0) {
        return controllers[0].connected ? controllers[0] : keyboard;
    }

    if (player == 1 && controllers[0].connected) {
        return merge_sources(keyboard, controllers[1]);
    }

    return {};
}

} // namespace forty_winks::input_routing
