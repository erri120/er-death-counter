#pragma once

#include <chrono>
#include <optional>
#include <stop_token>
#include <thread>

#include "game_data_man.hpp"
#include "logger.hpp"
#include "renderer.hpp"

namespace worker {

inline void run(std::stop_token st) {
    using namespace std::chrono_literals;
    using clock = std::chrono::steady_clock;

    constexpr auto boss_death_grace = 10s;

    std::uint32_t session          = 0;
    std::uint32_t prev_deaths      = 0;
    std::uint32_t tries            = 0;
    std::optional<std::uint32_t> last_fight_param;
    std::optional<clock::time_point> fight_end_time;
    bool          in_run           = false;
    bool          fight_was_active = false;

    while (!st.stop_requested()) {
        const auto deaths = er::death_count();
        const auto active = er::boss_fight_active();
        const auto param  = er::boss_npc_param_id();

        const bool running  = er::in_game() == true;
        const bool in_fight = active == true && param && *param != 0;

        if (!running || !deaths) {
            in_run           = false;
            fight_was_active = false;
            last_fight_param = std::nullopt;
            fight_end_time   = std::nullopt;
            renderer::set_death_count(std::nullopt, session, std::nullopt);
        } else {
            const auto value = *deaths;

            if (!in_run) {
                in_run = true;
                prev_deaths = value;
                const auto pt = er::play_time();
                logger::log("[death-counter] run started, baseline={}, playtime={}ms",
                            value, pt ? *pt : 0u);
            }

            if (in_fight) {
                if (!fight_was_active) {
                    if (!last_fight_param || *last_fight_param != *param) {
                        tries = 0;
                    }
                    last_fight_param = *param;
                    const auto eid = er::boss_entity_id();
                    logger::log("[death-counter] boss fight started, param={}, entity={}",
                                *param, eid ? *eid : 0u);
                }
                fight_end_time = std::nullopt;
            } else if (fight_was_active) {
                fight_end_time = clock::now();
                logger::log("[death-counter] boss fight ended");
            }

            bool boss_death = false;
            if (value > prev_deaths) {
                const bool in_grace = last_fight_param && fight_end_time &&
                                      clock::now() - *fight_end_time <
                                          boss_death_grace;
                if (in_fight || in_grace) {
                    boss_death = true;
                    session += value - prev_deaths;
                    tries += value - prev_deaths;
                    logger::log("[death-counter] boss death, total={}, session={}, tries={}",
                                value, session, tries);
                } else {
                    session += value - prev_deaths;
                    logger::log("[death-counter] death, total={}, session={}",
                                value, session);
                }
            } else if (value < prev_deaths) {
                logger::log("[death-counter] save reloaded, re-baselined to {}",
                            value);
            }

            if (boss_death && !in_fight) {
                fight_end_time = std::nullopt;
            }

            if (last_fight_param && !in_fight && fight_end_time &&
                clock::now() - *fight_end_time >= boss_death_grace) {
                logger::log("[death-counter] boss defeated, param={}",
                            *last_fight_param);
                last_fight_param = std::nullopt;
            }

            renderer::set_death_count(value, session,
                                      last_fight_param
                                          ? std::optional<std::uint32_t>{tries}
                                          : std::nullopt);

            fight_was_active = in_fight;
            prev_deaths      = value;
        }

        std::this_thread::sleep_for(50ms);
    }
}

inline std::jthread start() {
    return std::jthread{run};
}

}

