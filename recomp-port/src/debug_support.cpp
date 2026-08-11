#include "debug_support.hpp"

#include <atomic>
#include <cstdint>

#include "funcs.h"
#include "recomp_support.h"

namespace {

constexpr uint32_t current_level_address = 0x8009AE58;
constexpr uint32_t scene_mode_address = 0x800A14A0;
constexpr uint32_t level_entry_address = 0x800A39AC;
constexpr uint32_t level_target_address = 0x800A39B0;
constexpr uint32_t transition_busy_address = 0x801C7CD0;
constexpr gpr level_transition_callback = S32(0x8007BB74);

std::atomic<int> current_level{-1};
std::atomic<int> scene_mode{-1};
std::atomic<int> requested_level{-1};
std::atomic<int> loading_level{-1};
std::atomic<bool> transition_busy{false};
std::atomic<forty_winks::debug::LevelLoadStatus> load_status{
    forty_winks::debug::LevelLoadStatus::Idle,
};

bool valid_level(int level) {
    return level >= 0 &&
        level < static_cast<int>(forty_winks::debug::level_names.size());
}

} // namespace

namespace forty_winks::debug {

bool request_level_load(int level) {
    if (!valid_level(level)) {
        load_status.store(LevelLoadStatus::Failed);
        return false;
    }

    requested_level.store(level);
    loading_level.store(-1);
    load_status.store(LevelLoadStatus::Requested);
    return true;
}

LevelState level_state() {
    return {
        .current_level = current_level.load(),
        .scene_mode = scene_mode.load(),
        .requested_level = requested_level.load(),
        .loading_level = loading_level.load(),
        .transition_busy = transition_busy.load(),
        .status = load_status.load(),
    };
}

const char* level_name(int level) {
    return valid_level(level) ? level_names[static_cast<size_t>(level)] : "Special map";
}

const char* level_load_status_text(LevelLoadStatus status) {
    switch (status) {
        case LevelLoadStatus::Idle: return "Ready";
        case LevelLoadStatus::Requested: return "Level requested";
        case LevelLoadStatus::WaitingForGameplay: return "Waiting for gameplay";
        case LevelLoadStatus::WaitingForTransition: return "Waiting for current transition";
        case LevelLoadStatus::Scheduled: return "Transition scheduled";
        case LevelLoadStatus::Loading: return "Loading level";
        case LevelLoadStatus::Complete: return "Level loaded";
        case LevelLoadStatus::AlreadyCurrent: return "Level is already active";
        case LevelLoadStatus::Failed: return "Level request failed";
    }
    return "Unknown";
}

} // namespace forty_winks::debug

extern "C" void dispatch_debug_level_command(uint8_t* rdram, recomp_context* ctx) {
    const int live_level = static_cast<int32_t>(MEM_W(0, S32(current_level_address)));
    const int live_mode = MEM_BU(0, S32(scene_mode_address));
    const bool live_transition_busy =
        MEM_BU(0, S32(transition_busy_address)) != 0;

    current_level.store(live_level);
    scene_mode.store(live_mode);
    transition_busy.store(live_transition_busy);

    const int active_load = loading_level.load();
    if (active_load >= 0) {
        if (live_level == active_load && live_mode == 11 && !live_transition_busy) {
            loading_level.store(-1);
            load_status.store(forty_winks::debug::LevelLoadStatus::Complete);
        } else {
            load_status.store(forty_winks::debug::LevelLoadStatus::Loading);
        }
    }

    const int target = requested_level.load();
    if (!valid_level(target)) {
        return;
    }
    if (live_mode != 11) {
        load_status.store(forty_winks::debug::LevelLoadStatus::WaitingForGameplay);
        return;
    }
    if (target == live_level) {
        requested_level.store(-1);
        load_status.store(forty_winks::debug::LevelLoadStatus::AlreadyCurrent);
        return;
    }
    if (live_transition_busy) {
        load_status.store(forty_winks::debug::LevelLoadStatus::WaitingForTransition);
        return;
    }

    // Action-3 doors write these same two fields before scheduling callback
    // 0x8007BB74. Entry zero is the common authored fallback spawn.
    MEM_W(0, S32(level_target_address)) = target;
    MEM_W(0, S32(level_entry_address)) = 0;

    const recomp_context saved_context = *ctx;
    ctx->r4 = level_transition_callback;
    func_8005BD34(rdram, ctx);
    *ctx = saved_context;

    if (MEM_BU(0, S32(transition_busy_address)) != 0) {
        requested_level.store(-1);
        loading_level.store(target);
        transition_busy.store(true);
        load_status.store(forty_winks::debug::LevelLoadStatus::Scheduled);
    } else {
        requested_level.store(-1);
        load_status.store(forty_winks::debug::LevelLoadStatus::Failed);
    }
}
