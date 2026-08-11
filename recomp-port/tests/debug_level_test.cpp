#include <cstdint>
#include <cstdio>
#include <vector>

#include "debug_support.hpp"
#include "recomp_support.h"

namespace {

constexpr uint32_t current_level_address = 0x8009AE58;
constexpr uint32_t scene_mode_address = 0x800A14A0;
constexpr uint32_t level_entry_address = 0x800A39AC;
constexpr uint32_t level_target_address = 0x800A39B0;
constexpr uint32_t transition_busy_address = 0x801C7CD0;
constexpr uint32_t expected_callback = 0x8007BB74;

uint32_t scheduled_callback = 0;
int schedule_count = 0;

bool check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "Debug level test failed: %s\n", message);
    }
    return condition;
}

void write_word(uint8_t* rdram, uint32_t address, uint32_t value) {
    MEM_W(0, S32(address)) = static_cast<int32_t>(value);
}

uint32_t read_word(uint8_t* rdram, uint32_t address) {
    return static_cast<uint32_t>(MEM_W(0, S32(address)));
}

void write_byte(uint8_t* rdram, uint32_t address, uint8_t value) {
    MEM_B(0, S32(address)) = static_cast<int8_t>(value);
}

} // namespace

extern "C" void func_8005BD34(uint8_t* rdram, recomp_context* ctx) {
    scheduled_callback = static_cast<uint32_t>(ctx->r4);
    ++schedule_count;
    write_byte(rdram, transition_busy_address, 1);
    ctx->r2 = 0x12345678;
    ctx->r4 = 0x23456789;
    ctx->r29 = 0x3456789A;
}

int main() {
    std::vector<uint8_t> memory(8 * 1024 * 1024);
    uint8_t* rdram = memory.data();

    write_word(rdram, current_level_address, 1);
    write_byte(rdram, scene_mode_address, 11);
    write_byte(rdram, transition_busy_address, 0);
    write_word(rdram, level_entry_address, 7);

    recomp_context context{};
    context.r2 = 0x11111111;
    context.r4 = 0x22222222;
    context.r29 = 0x33333333;

    if (!check(forty_winks::debug::request_level_load(8), "accept valid level") ) {
        return 1;
    }
    dispatch_debug_level_command(rdram, &context);

    const auto scheduled = forty_winks::debug::level_state();
    if (!check(schedule_count == 1, "schedule original transition") ||
        !check(scheduled_callback == expected_callback, "transition callback address") ||
        !check(read_word(rdram, level_target_address) == 8, "target level field") ||
        !check(read_word(rdram, level_entry_address) == 0, "default entry field") ||
        !check(context.r2 == 0x11111111 && context.r4 == 0x22222222 &&
                   context.r29 == 0x33333333,
               "preserve interrupted game registers") ||
        !check(scheduled.loading_level == 8, "track scheduled level") ||
        !check(scheduled.status == forty_winks::debug::LevelLoadStatus::Scheduled,
               "scheduled status")) {
        return 1;
    }

    write_word(rdram, current_level_address, 8);
    write_byte(rdram, scene_mode_address, 11);
    write_byte(rdram, transition_busy_address, 0);
    dispatch_debug_level_command(rdram, &context);
    if (!check(forty_winks::debug::level_state().status ==
                   forty_winks::debug::LevelLoadStatus::Complete,
               "complete after loader reaches target")) {
        return 1;
    }

    if (!check(forty_winks::debug::request_level_load(8), "accept current level") ) {
        return 1;
    }
    dispatch_debug_level_command(rdram, &context);
    if (!check(schedule_count == 1, "do not reschedule current level") ||
        !check(forty_winks::debug::level_state().status ==
                   forty_winks::debug::LevelLoadStatus::AlreadyCurrent,
               "already-current status")) {
        return 1;
    }

    write_byte(rdram, scene_mode_address, 26);
    if (!check(forty_winks::debug::request_level_load(9), "accept deferred level") ) {
        return 1;
    }
    dispatch_debug_level_command(rdram, &context);
    if (!check(schedule_count == 1, "do not schedule outside gameplay") ||
        !check(forty_winks::debug::level_state().status ==
                   forty_winks::debug::LevelLoadStatus::WaitingForGameplay,
               "wait for gameplay status")) {
        return 1;
    }

    std::printf("Debug level transition test passed.\n");
    return 0;
}
