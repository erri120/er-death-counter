#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include <windows.h>

namespace er {

inline constexpr std::uintptr_t kGameDataManRva = 0x3d61f98;
inline constexpr std::ptrdiff_t kDeathCountOffset          = 0x94;
inline constexpr std::ptrdiff_t kPlayTimeOffset            = 0xa0;
inline constexpr std::ptrdiff_t kBossFightActiveOffset     = 0xc0;
inline constexpr std::ptrdiff_t kBossEntityIdOffset        = 0xc8;
inline constexpr std::ptrdiff_t kBossNpcParamIdOffset      = 0xcc;

inline std::uintptr_t game_data_man() {
    const auto base = reinterpret_cast<std::uintptr_t>(
        ::GetModuleHandleW(L"eldenring.exe"));
    if (base == 0) {
        return 0;
    }

    const auto gd_ptr = *reinterpret_cast<std::uintptr_t*>(
        base + kGameDataManRva);
    if (gd_ptr == 0) {
        return 0;
    }

    return gd_ptr;
}

inline std::optional<std::uint32_t> death_count() {
    const auto gd = game_data_man();
    if (gd == 0) {
        return std::nullopt;
    }

    return *reinterpret_cast<std::uint32_t*>(gd + kDeathCountOffset);
}

inline std::optional<bool> in_game() {
    const auto gd = game_data_man();
    if (gd == 0) {
        return std::nullopt;
    }

    return *reinterpret_cast<std::uint32_t*>(gd + kPlayTimeOffset) != 0;
}

inline std::optional<std::uint32_t> play_time() {
    const auto gd = game_data_man();
    if (gd == 0) {
        return std::nullopt;
    }

    return *reinterpret_cast<std::uint32_t*>(gd + kPlayTimeOffset);
}

inline std::optional<bool> boss_fight_active() {
    const auto gd = game_data_man();
    if (gd == 0) {
        return std::nullopt;
    }

    return *reinterpret_cast<bool*>(gd + kBossFightActiveOffset);
}

inline std::optional<std::uint32_t> boss_entity_id() {
    const auto gd = game_data_man();
    if (gd == 0) {
        return std::nullopt;
    }

    return *reinterpret_cast<std::uint32_t*>(gd + kBossEntityIdOffset);
}

inline std::optional<std::uint32_t> boss_npc_param_id() {
    const auto gd = game_data_man();
    if (gd == 0) {
        return std::nullopt;
    }

    return *reinterpret_cast<std::uint32_t*>(gd + kBossNpcParamIdOffset);
}

}

