#pragma once

#include <array>

namespace forty_winks::debug {

enum class LevelLoadStatus {
    Idle,
    Requested,
    WaitingForGameplay,
    WaitingForTransition,
    Scheduled,
    Loading,
    Complete,
    AlreadyCurrent,
    Failed,
};

struct LevelState {
    int current_level;
    int scene_mode;
    int requested_level;
    int loading_level;
    bool transition_busy;
    LevelLoadStatus status;
};

inline constexpr std::array<const char*, 38> level_names{
    "House Hub",
    "Nightmare Hub",
    "Underwater Hub",
    "Space Hub",
    "Prehistoric Hub",
    "Castle Hub",
    "Pirate Hub",
    "Haunted House",
    "Cemetery",
    "Scary Woods",
    "Nightmare Race",
    "Shipwreck",
    "Complex",
    "Atlantis",
    "Underwater Race",
    "Space Race",
    "Space Port",
    "Space Station",
    "Moon Caves",
    "Outside",
    "Temple",
    "Swamp",
    "Dinosaur Race",
    "Castle",
    "Ruins",
    "Dungeons",
    "Dragon Race",
    "Treasure Island",
    "Shipwreck City",
    "Scary Mary",
    "Galleon Racing",
    "Nightmare Boss",
    "Underwater Boss",
    "Space Boss",
    "Prehistoric Boss",
    "Castle Boss",
    "Pirate Boss",
    "Nitekap Boss",
};

bool request_level_load(int level);
LevelState level_state();
const char* level_name(int level);
const char* level_load_status_text(LevelLoadStatus status);

} // namespace forty_winks::debug
