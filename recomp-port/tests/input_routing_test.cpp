#include <array>
#include <cstdio>

#include "input_routing.hpp"

namespace {

using forty_winks::input_routing::InputSample;
using forty_winks::input_routing::player_count;
using forty_winks::input_routing::route_player;

bool check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
    }
    return condition;
}

} // namespace

int main() {
    bool passed = true;
    const InputSample keyboard{
        .connected = true,
        .buttons = 0x8000,
        .x = 1.0f,
        .y = 0.0f,
    };

    std::array<InputSample, player_count> controllers{};
    InputSample player_one = route_player(0, keyboard, controllers);
    InputSample player_two = route_player(1, keyboard, controllers);
    passed &= check(player_one.connected && player_one.buttons == keyboard.buttons,
        "keyboard should fall back to Player 1 without a gamepad");
    passed &= check(!player_two.connected,
        "Player 2 should remain disconnected in keyboard-only mode");

    controllers[0] = {
        .connected = true,
        .buttons = 0x4000,
        .x = -0.5f,
        .y = 0.25f,
    };
    player_one = route_player(0, keyboard, controllers);
    player_two = route_player(1, keyboard, controllers);
    passed &= check(player_one.buttons == controllers[0].buttons && player_one.x == -0.5f,
        "first gamepad should exclusively control Player 1");
    passed &= check(player_two.connected && player_two.buttons == keyboard.buttons,
        "keyboard should move to Player 2 when Player 1 has a gamepad");

    controllers[1] = {
        .connected = true,
        .buttons = 0x2000,
        .x = 0.25f,
        .y = -0.75f,
    };
    player_two = route_player(1, keyboard, controllers);
    passed &= check(player_two.buttons == 0xA000,
        "keyboard and second-gamepad buttons should merge for Player 2");
    passed &= check(player_two.x == 1.0f && player_two.y == -0.75f,
        "Player 2 should use the strongest axis from either input source");

    passed &= check(!route_player(-1, keyboard, controllers).connected,
        "negative player indices should be rejected");
    passed &= check(!route_player(2, keyboard, controllers).connected,
        "ports beyond Player 2 should be rejected");

    if (passed) {
        std::printf("Two-player input routing tests passed.\n");
        return 0;
    }
    return 1;
}
